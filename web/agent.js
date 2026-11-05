// The Elite agent on a jsbeeb machine: agent.wasm (the C player) reads the machine's main RAM each frame and presses
// its keys and moves its joystick. The same in the browser (index.html) and in Node (run_node.mjs).
import { wasiShim } from "./wasi_shim.js";

export class Agent {
    /** cpu: jsbeeb's processor; keyboard: its keyboard (keyDownRaw/keyUpRaw); hooks: { log(level, line), note(json) } */
    static async create(wasmBytes, cpu, keyboard, hooks = {}) {
        const a = new Agent();
        a.cpu = cpu; a.keyboard = keyboard; a.hooks = hooks;
        a.adc = [0x8000, 0x8000, 0x8000, 0x8000];
        const dec = new TextDecoder();
        let mem = null;
        const str = (p, n) => dec.decode(new Uint8Array(mem.buffer, p, n));
        const env = {
            key: (code, down) => (down ? keyboard.keyDownRaw : keyboard.keyUpRaw).call(keyboard, [code & 15, (code >> 4) & 7]),
            analogue: (ch, v) => { a.adc[ch & 3] = v; },
            log: (level, p, n) => hooks.log && hooks.log(level, str(p, n)),
            note: (p, n) => hooks.note && hooks.note(str(p, n)),
        };
        const { instance } = await WebAssembly.instantiate(wasmBytes, { env, wasi_snapshot_preview1: wasiShim(() => new Uint8Array(mem.buffer)) });
        mem = instance.exports.memory;
        instance.exports._initialize();
        a.x = instance.exports;
        // the joystick: jsbeeb's ADC reads each channel from a source; ours reads what the player last set
        const src = { getValue: (ch) => a.adc[ch & 3], dispose() {} };
        for (let ch = 0; ch < 4; ch++) cpu.adconverter.setChannelSource(ch, src);
        return a;
    }
    /** the 32 KB of main RAM (on the Master: &0000-&7FFF of its RAM, whatever the shadow paging) into the player's copy */
    syncRam() {
        if (!this.ram || this.ram.buffer !== this.x.memory.buffer) this.ram = new Uint8Array(this.x.memory.buffer, this.x.wasm_ram(), 0x8000);
        this.ram.set(this.cpu.ramRomOs.subarray(0, 0x8000));
    }
    /** the commander's name, what the player saves as (up to 7 letters) */
    set name(n) { const b = new Uint8Array(this.x.memory.buffer, this.x.wasm_name(), 8); b.fill(0); b.set(new TextEncoder().encode(n.toUpperCase().slice(0, 7))); }
    /** a commander for the player to load from the disc when next docked (its file E.<name>) */
    set load(n) { const b = new Uint8Array(this.x.memory.buffer, this.x.wasm_load(), 8); b.fill(0); b.set(new TextEncoder().encode(n.toUpperCase().slice(0, 7))); }
    /** mode: 0 Optimal, 1 Fair, 2 Curious. True once the machine's RAM holds an Elite the player knows */
    start(mode = 1, machine = 3) { this.syncRam(); return this.x.wasm_init(mode, machine) !== 0; }
    get tally() { return this.x.wasm_tally(); }
    get cash() { return this.x.wasm_cash() / 10; }
    /** the networks' senses at their last decisions and when (the player's frame) */
    senses(which) {
        const n = which === "fight" ? 23 : 47;
        const p = which === "fight" ? this.x.wasm_fight_senses() : this.x.wasm_dock_senses();
        return { when: which === "fight" ? this.x.wasm_fight_when() : this.x.wasm_dock_when(), f: new Float32Array(this.x.memory.buffer, p, n).slice() };
    }
    /** 1 in a fight, 2 in the docking's final, else 0 */
    get action() { return this.x.wasm_action(); }
    /** after each emulated frame */
    frame() { this.syncRam(); this.x.wasm_frame(); }
    get frameNo() { return this.x.wasm_frame_no(); }
}
