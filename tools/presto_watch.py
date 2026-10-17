"""Watch the C player on the Presto over USB. STATUS lines (one short line per event) to
logs/presto-c-status.log and the terminal; PLAYER and DEBUG lines, and the firmware's STATS, to
logs/presto-c.log; NOTE lines (its journal) to logs/presto-c-journal.jsonl. --start sends !player first (it
toggles: only when the player is off). The Presto is found by its USB serial number only."""
import json, os, sys, termios, time

PORT = os.environ.get('PRESTO_PORT', '/dev/serial/by-id/usb-Raspberry_Pi_Pico_CA294DE20D3BAF57-if00')


def main():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[3] &= ~(termios.ICANON | termios.ECHO)
    termios.tcsetattr(fd, termios.TCSANOW, a)
    if '--start' in sys.argv:
        os.write(fd, b'!player\n')
    os.makedirs('logs', exist_ok=True)
    log = open('logs/presto-c.log', 'a')
    jl = open('logs/presto-c-journal.jsonl', 'a')
    st = open('logs/presto-c-status.log', 'a')
    buf = b''
    while True:
        buf += os.read(fd, 4096)
        *lines, buf = buf.split(b'\n')
        for raw in lines:
            line = raw.decode('utf-8', 'replace').rstrip('\r')
            stamp = time.strftime('%H:%M:%S')
            if line.startswith('STATUS '):
                text = line.split(' ', 2)[-1]
                st.write(f'{stamp} {text}\n')
                st.flush()
                print(stamp, text, flush=True)
            elif line.startswith('NOTE '):
                _, frame, js = line.split(' ', 2)
                jl.write(json.dumps({'t': time.time(), 'frame': int(frame), 'e': json.loads(js)}) + '\n')
                jl.flush()
            elif line.startswith('STATS '):
                log.write(f'{stamp} {line}\n')
                log.flush()
            elif line.startswith(('PLAYER ', 'DEBUG ')):
                log.write(f'{stamp} {line.split(" ", 1)[1]}\n')
                log.flush()


if __name__ == '__main__':
    main()
