// core.wasm from JavaScript: the machine and the player in one object, the same in the browser and in Node.
import { wasiShim } from "./wasi_shim.js";

export class Core {
    /** bytes: core.wasm; roms: 8 of 16 KB (the MOS, then slots 9 to 15); disc: an .ssd; hooks: { log(level, line), note(json) } */
    static async create(bytes, roms, disc, hooks = {}) {
        const c = new Core();
        const dec = new TextDecoder();
        let mem = null;
        const str = (p, n) => dec.decode(new Uint8Array(mem.buffer, p, n));
        const env = {
            key: (code, down) => c.x.core_key(code, down),
            analogue: (ch, v) => c.x.core_analogue(ch, v),
            log: (level, p, n) => hooks.log && hooks.log(level, str(p, n)),
            note: (p, n) => hooks.note && hooks.note(str(p, n)),
        };
        const { instance } = await WebAssembly.instantiate(bytes, { env, wasi_snapshot_preview1: wasiShim(() => new Uint8Array(mem.buffer)) });
        mem = instance.exports.memory;
        c.x = instance.exports;
        c.x._initialize();
        const put = (b) => { const p = c.x.core_alloc(b.length); new Uint8Array(mem.buffer, p, b.length).set(b); return p; };
        const ptrs = roms.map(put), table = c.x.core_alloc(4 * ptrs.length);
        new Uint32Array(mem.buffer, table, ptrs.length).set(ptrs);
        const d = put(disc);
        c.x.core_boot(table, d, disc.length);
        c.width = c.x.core_width(); c.height = c.x.core_height();
        return c;
    }
    start(skew = 13, mode = 1) { return this.x.core_start(skew, mode) !== 0; }
    frame(draw = false) { this.x.core_frame(draw ? 1 : 0); }
    /** the last drawn picture: RGBA words, width x height */
    rgba() { const p = this.x.core_rgba(); return new Uint32Array(this.x.memory.buffer, p, this.width * this.height); }
    /** the sound chip's next 625 samples at 31,250 Hz */
    audio() { const p = this.x.core_audio(); return new Int16Array(this.x.memory.buffer, p, 625); }
    set name(n) { const b = new Uint8Array(this.x.memory.buffer, this.x.wasm_name(), 8); b.fill(0); b.set(new TextEncoder().encode(n.toUpperCase().slice(0, 7))); }
    /** a commander to load from the disc when next docked (its file E.<name>) */
    set load(n) { const b = new Uint8Array(this.x.memory.buffer, this.x.wasm_load(), 8); b.fill(0); b.set(new TextEncoder().encode(n.toUpperCase().slice(0, 7))); }
    /** n bytes of the machine's main RAM, as the player saw it this frame */
    ram(addr, n) { return new Uint8Array(this.x.memory.buffer, this.x.wasm_ram() + addr, n).slice(); }
    get tally() { return this.x.wasm_tally(); }
    get cash() { return this.x.wasm_cash() / 10; }
    get action() { return this.x.wasm_action(); }
    get frameNo() { return this.x.wasm_frame_no(); }
    /** the dogfight network that decided last: "fight" (fair mode's), "camera" or "interceptor" */
    get fightNet() { return ["fight", "camera", "interceptor"][this.x.wasm_fight_net()] || "fight"; }
    senses(which) {
        const n = which === "fight" ? 23 : 47;
        const p = which === "fight" ? this.x.wasm_fight_senses() : this.x.wasm_dock_senses();
        return { when: which === "fight" ? this.x.wasm_fight_when() : this.x.wasm_dock_when(), f: new Float32Array(this.x.memory.buffer, p, n).slice() };
    }
}
