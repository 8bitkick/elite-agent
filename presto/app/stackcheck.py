#!/usr/bin/env python3
"""stackcheck.py <build dir> <stack_size> <entry> ...: the app's worst-case stack, from gcc's -fstack-usage and
-fcallgraph-info=su (the .ci files beside the objects), along the deepest call chain from each entry point the
firmware calls. Fails (exit 1) if any is over stack_size: the app runs on core 0's stack, and past it is core 1's.

Conservative where the graph can't see:
- an indirect call (a function pointer) is taken to call the deepest of what POINTERS says that caller can call: the
  player's IO callbacks (elite_presto.c's p_*), the condition functions handed to the waits (every address-taken
  is_* and has_*), or the firmware (app_api: FIRMWARE_ALLOW). A caller not in POINTERS, or a function whose address is
  taken that nothing in POINTERS covers, fails the check: a new pointer has to be added here;
- a call out of the app's own code is allowed LIB_ALLOW bytes into libc, libm, libgcc and the SDK's maths (leaf
  routines: memset, the double coprocessor's wrappers), and FIRMWARE_ALLOW into the firmware through app_api;
- a recursion is an error (none now).
A protothread's CO_CALL is a plain call: its frame stays under the child's, so the graph has it right."""
import glob, os, re, subprocess, sys

LIB_ALLOW = 128         # (the libraries' leaf routines)
FIRMWARE_ALLOW = 512    # (the firmware's log line to USB and the panel)
CONDS = 'conds'         # (the address-taken is_* and has_*)
FIRMWARE = 'firmware'   # (app_api's functions, the machine's: FIRMWARE_ALLOW)
POINTERS = {
    'hold': ['p_hold', 'p_analogue'], 'press': ['p_tap'], 'type_text': ['p_tap'], 'note': ['p_note'],
    'logf_': ['p_log'], 'dbgf_': ['p_log'], 'status_': ['p_log'], 'keyq_step': ['key'], 'keyq_release': ['key'],
    'plan_stick': ['plan_rates', 'plan_rates_f'],
    'bfs': [CONDS], 'wait_for': [CONDS], 'hold_until': [CONDS], 'settle': [CONDS],
    'elite_frame': [FIRMWARE], 'key': [FIRMWARE], 'p_analogue': [FIRMWARE], 'say': [FIRMWARE],
    'p_hold': [FIRMWARE], 'p_tap': [FIRMWARE], 'elite_init': [FIRMWARE], 'elite_command': [FIRMWARE],
}


def main():
    d, limit, entries = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
    size, edges, byname = {}, {}, {}
    for f in glob.glob(os.path.join(d, '*.ci')):
        for line in open(f):
            m = re.match(r'node: \{ title: "([^"]+)" label: "([^\\]+)\\n[^\\]*\\n(\d+) bytes \((\w+)', line)
            if m:
                t, n, s, kind = m.groups()
                if kind != 'static':
                    sys.exit('stackcheck: %s has a %s stack frame' % (n, kind))
                size[t] = int(s)
                byname.setdefault(n, []).append(t)
                continue
            m = re.match(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', line)
            if m:
                edges.setdefault(m.group(1), set()).add(m.group(2))
    # functions whose address is taken: any relocation to a function's own section that isn't a call
    taken = set()
    for o in glob.glob(os.path.join(d, '*.o')):
        if os.path.basename(o).startswith('sdk_'):
            continue
        out = subprocess.run(['arm-none-eabi-readelf', '-rW', o], capture_output=True, text=True).stdout
        sec = ''
        for line in out.splitlines():
            m = re.match(r"Relocation section '([^']+)'", line)
            if m:
                sec = m.group(1)
                continue
            if sec.startswith(('.rel.debug', '.rel.ARM')):
                continue        # (the debug info names every function: not an address taken)
            p = line.split()
            if len(p) >= 5 and p[2].startswith('R_ARM_') and p[2] not in ('R_ARM_THM_CALL', 'R_ARM_THM_JUMP24', 'R_ARM_CALL', 'R_ARM_JUMP24'):
                sym = p[4]
                if sec == '.rel' + sym:
                    continue        # (a protothread's own labels, as values)
                if sym.startswith('.text.'):
                    taken.add(sym[6:])
                elif sym in byname:
                    taken.add(sym)
    sys.setrecursionlimit(20000)
    memo, onstack = {}, set()

    conds = sorted(n for n in taken if n.startswith(('is_', 'has_')))
    covered = set(conds) | set(entries) | {x for v in POINTERS.values() for x in v}
    loose = sorted(n for n in taken if n not in covered)
    if loose:
        sys.exit('stackcheck: address taken, called through what? %s (add it to POINTERS)' % ' '.join(loose))

    def targets(t, caller=None):
        if t == '__indirect_call':
            name = caller.split(':')[-1].split('.')[0]
            if name not in POINTERS:
                sys.exit('stackcheck: %s calls through a pointer: say what to in POINTERS' % name)
            out = []
            for v in POINTERS[name]:
                if v == FIRMWARE:
                    out.append(None)
                else:
                    for n in (conds if v == CONDS else [v]):
                        out += byname.get(n, [])
            return out
        if t in size:
            return [t]
        return byname.get(t.split(':')[-1], [])

    def worst(t):
        if t in memo:
            return memo[t]
        if t in onstack:
            sys.exit('stackcheck: recursion through %s' % t)
        onstack.add(t)
        best, path = 0, []
        for e in edges.get(t, ()):
            for r in targets(e, t) or [None]:
                if r is None:       # (outside the app)
                    a = FIRMWARE_ALLOW if e == '__indirect_call' else LIB_ALLOW
                    w, p = a, ['%s (outside the app: %d allowed)' % ('firmware' if e == '__indirect_call' else e, a)]
                else:
                    w, p = worst(r)
                if w > best:
                    best, path = w, p
        onstack.discard(t)
        memo[t] = (size.get(t, 0) + best, ['%s %d' % (t.split(':')[-1], size.get(t, 0))] + path)
        return memo[t]

    bad = 0
    for e in entries:
        roots = byname.get(e, [])
        if not roots:
            sys.exit('stackcheck: no entry point %s' % e)
        w, path = worst(roots[0])
        print('stack: %-20s %5d bytes of %d%s' % (e, w, limit, '' if w <= limit else '  OVER'))
        if w > limit or '-v' in os.environ.get('STACKCHECK', ''):
            print('   ' + ' > '.join(path))
        bad |= w > limit
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
