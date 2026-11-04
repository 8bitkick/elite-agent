// The commander's save as an .ssd of its own: the game disc's DFS catalogue read, the commander's file copied onto a new
// single-sided disc with nothing else on it (load it in Elite with "Load New Commander": Y, then the name).
const SECTOR = 256, TRACK = 10 * SECTOR, DISC_SECTORS = 800;

function catalogue(img) {
    const files = [], n = img[SECTOR + 5] >> 3;
    for (let i = 1; i <= n; i++) {
        const a = 8 * i, b = SECTOR + 8 * i;
        const name = String.fromCharCode(...img.subarray(a, a + 7)).replace(/ +$/, "");
        const dir = String.fromCharCode(img[a + 7] & 0x7f);
        const mixed = img[b + 6];
        const load = img[b] | (img[b + 1] << 8) | (((mixed >> 2) & 3) << 16);
        const exec = img[b + 2] | (img[b + 3] << 8) | (((mixed >> 6) & 3) << 16);
        const length = img[b + 4] | (img[b + 5] << 8) | (((mixed >> 4) & 3) << 16);
        const start = img[b + 7] | ((mixed & 3) << 8);
        files.push({ name, dir, load, exec, length, start });
    }
    return files;
}

/** a .ssd holding only `name` from the disc image `img`, or null if the disc has no such file */
export function commanderSsd(img, name) {
    const f = catalogue(img).find((x) => x.name.toUpperCase() === name.toUpperCase());
    if (!f) return null;
    const data = img.subarray(f.start * SECTOR, f.start * SECTOR + f.length);
    const sectors = 2 + Math.ceil(f.length / SECTOR);
    const out = new Uint8Array(Math.ceil((sectors * SECTOR) / TRACK) * TRACK);
    const title = name.toUpperCase().padEnd(12, " ").slice(0, 12);
    for (let i = 0; i < 8; i++) out[i] = title.charCodeAt(i);
    for (let i = 0; i < 4; i++) out[SECTOR + i] = title.charCodeAt(8 + i);
    const fname = f.name.padEnd(7, " ");
    for (let i = 0; i < 7; i++) out[8 + i] = fname.charCodeAt(i);
    out[15] = f.dir.charCodeAt(0);
    out[SECTOR + 4] = 0;                                  // the catalogue's cycle number
    out[SECTOR + 5] = 8;                                  // one file
    out[SECTOR + 6] = (DISC_SECTORS >> 8) & 3;            // no boot option; the disc's size
    out[SECTOR + 7] = DISC_SECTORS & 0xff;
    const b = SECTOR + 8, start = 2;
    out[b] = f.load & 0xff; out[b + 1] = (f.load >> 8) & 0xff;
    out[b + 2] = f.exec & 0xff; out[b + 3] = (f.exec >> 8) & 0xff;
    out[b + 4] = f.length & 0xff; out[b + 5] = (f.length >> 8) & 0xff;
    out[b + 6] = (((f.exec >> 16) & 3) << 6) | (((f.length >> 16) & 3) << 4) | (((f.load >> 16) & 3) << 2) | ((start >> 8) & 3);
    out[b + 7] = start & 0xff;
    out.set(data, start * SECTOR);
    return out;
}

/** a copy of the disc image `img` with the file `dir`.`name` added (load and exec 0, as Elite saves a commander) */
export function addFile(img, name, dir, data) {
    const files = catalogue(img), n = files.length;
    if (n >= 31) throw new Error("the disc's catalogue is full");
    const start = Math.max(2, ...files.map((f) => f.start + Math.ceil(f.length / SECTOR)));
    const out = new Uint8Array(Math.max(img.length, Math.ceil(((start + Math.ceil(data.length / SECTOR)) * SECTOR) / TRACK) * TRACK));
    out.set(img);
    out.set(data, start * SECTOR);
    // the catalogue runs from the highest start sector down: the new file goes first
    out.copyWithin(16, 8, 8 + 8 * n); out.copyWithin(SECTOR + 16, SECTOR + 8, SECTOR + 8 + 8 * n);
    const fname = name.toUpperCase().padEnd(7, " ");
    for (let i = 0; i < 7; i++) out[8 + i] = fname.charCodeAt(i);
    out[15] = dir.charCodeAt(0);
    const b = SECTOR + 8;
    out.fill(0, b, b + 4);
    out[b + 4] = data.length & 0xff; out[b + 5] = (data.length >> 8) & 0xff;
    out[b + 6] = (((data.length >> 16) & 3) << 4) | ((start >> 8) & 3);
    out[b + 7] = start & 0xff;
    out[SECTOR + 5] = (n + 1) * 8;
    return out;
}

/** a copy of the disc image `img` without the file `dir`.`name` (the catalogue closed up; its sectors left as they are) */
export function removeFile(img, name, dir) {
    const files = catalogue(img), k = files.findIndex((f) => f.name.toUpperCase() === name.toUpperCase() && f.dir === dir);
    if (k < 0) return img;
    const out = img.slice(), n = files.length, a = 8 * (k + 1);
    out.copyWithin(a, a + 8, 8 + 8 * n); out.copyWithin(SECTOR + a, SECTOR + a + 8, SECTOR + 8 + 8 * n);
    out.fill(0, 8 * n, 8 * n + 8); out.fill(0, SECTOR + 8 * n, SECTOR + 8 * n + 8);
    out[SECTOR + 5] = (n - 1) * 8;
    return out;
}

export { catalogue };
