// The few WASI calls the player's C library makes, for the browser and for Node without its wasi module: no files, no
// environment, stdout and stderr to a callback.
export function wasiShim(getMemory, write = (fd, s) => (fd === 2 ? console.error : console.log)(s)) {
    const dec = new TextDecoder();
    const ENOSYS = 52, EBADF = 8;
    return {
        environ_get: () => 0,
        environ_sizes_get: (countPtr, sizePtr) => { const v = new DataView(getMemory().buffer); v.setUint32(countPtr, 0, true); v.setUint32(sizePtr, 0, true); return 0; },
        fd_write: (fd, iovs, n, nwritten) => {
            const m = getMemory(), v = new DataView(m.buffer); let total = 0, s = "";
            for (let i = 0; i < n; i++) { const p = v.getUint32(iovs + 8 * i, true), l = v.getUint32(iovs + 8 * i + 4, true); s += dec.decode(m.subarray(p, p + l)); total += l; }
            if (s.length) write(fd, s.replace(/\n$/, ""));
            v.setUint32(nwritten, total, true); return 0;
        },
        fd_close: () => 0, fd_seek: () => ENOSYS, fd_read: () => EBADF, fd_fdstat_get: () => EBADF, fd_fdstat_set_flags: () => ENOSYS,
        fd_prestat_get: () => EBADF, fd_prestat_dir_name: () => EBADF, path_open: () => ENOSYS,
        // (a newer wasi-libc asks for the time: always 0, so nothing can depend on it)
        clock_time_get: (id, precision, ptr) => { new DataView(getMemory().buffer).setBigUint64(ptr, 0n, true); return 0; },
        proc_exit: (code) => { throw new Error("player exited " + code); },
    };
}
