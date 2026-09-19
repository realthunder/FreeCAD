// A minimal wasi_snapshot_preview1 host for the expression sandbox image
// (docs/ExpressionSandbox.md sec 6, docs/ExpressionImage.md "The browser
// tier").  The desktop host gets the image's confinement from wasmtime's WASI
// implementation; in the browser this file IS that boundary, so what it does
// not implement, the image cannot do.
//
// The image needs exactly one capability from outside: a read-only /Lib
// holding the handful of CPython files that are not frozen into the
// interpreter.  There is no network, no spawning, no writable file, no host
// filesystem -- not because a policy layer refuses, but because the
// operations are absent.  Keep it that way: every addition here widens the
// sandbox for every document the viewer will ever open.

const ERR = {
  SUCCESS: 0,
  BADF: 8,
  INVAL: 28,
  NOENT: 44,
  NOSYS: 52,
  NOTDIR: 54,
  NOTSUP: 58,
  ROFS: 69,
  SPIPE: 70,
} as const;

const FILETYPE = { CHARACTER_DEVICE: 2, DIRECTORY: 3, REGULAR_FILE: 4 } as const;

// path_open oflags
const O_CREAT = 1, O_DIRECTORY = 2, O_EXCL = 4, O_TRUNC = 8;

type Node =
  | { type: 'dir'; children: Map<string, Node> }
  | { type: 'file'; data: Uint8Array };

type Fd =
  | { kind: 'stdin' }
  | { kind: 'stream'; which: 'out' | 'err'; partial: string }
  | { kind: 'dir'; node: Node }
  | { kind: 'file'; node: Extract<Node, { type: 'file' }>; pos: number };

export interface WasiOptions {
  onStdout?: (line: string) => void;
  onStderr?: (line: string) => void;
}

/// Build the read-only tree from a flat {path: bytes} map.
export function makeTree(files: Record<string, Uint8Array>): Node {
  const root: Node = { type: 'dir', children: new Map() };
  for (const [path, data] of Object.entries(files)) {
    const parts = path.split('/').filter(Boolean);
    let node = root;
    for (let i = 0; i < parts.length - 1; i++) {
      let next = (node as any).children.get(parts[i]);
      if (!next) {
        next = { type: 'dir', children: new Map() };
        (node as any).children.set(parts[i], next);
      }
      node = next;
    }
    (node as any).children.set(parts[parts.length - 1], { type: 'file', data });
  }
  return root;
}

export class Wasi {
  memory: WebAssembly.Memory | null = null;
  /// Every path_open the image attempted, for the acceptance page's report.
  readonly opens: string[] = [];

  private fds = new Map<number, Fd>();
  private nextFd = 4;
  private onStdout: (line: string) => void;
  private onStderr: (line: string) => void;

  constructor(private root: Node, opts: WasiOptions = {}) {
    this.onStdout = opts.onStdout ?? ((s) => console.log('[fcx]', s));
    this.onStderr = opts.onStderr ?? ((s) => console.warn('[fcx]', s));
    // 0/1/2 are the standard streams; 3 is the single preopen, /Lib.
    this.fds.set(0, { kind: 'stdin' });
    this.fds.set(1, { kind: 'stream', which: 'out', partial: '' });
    this.fds.set(2, { kind: 'stream', which: 'err', partial: '' });
    this.fds.set(3, { kind: 'dir', node: root });
  }

  private get view() { return new DataView(this.memory!.buffer); }
  private get bytes() { return new Uint8Array(this.memory!.buffer); }
  private str(ptr: number, len: number) {
    return new TextDecoder().decode(this.bytes.subarray(ptr, ptr + len));
  }

  /// Resolve a path beneath a directory node.  '..' is refused outright
  /// rather than normalized: the image has no business climbing, and a
  /// resolver that never ascends cannot be tricked into it.
  private resolve(node: Node, path: string): Node | null {
    let cur: Node | null = node;
    for (const part of path.split('/')) {
      if (!part.length || part === '.') continue;
      if (part === '..') return null;
      if (!cur || cur.type !== 'dir') return null;
      cur = cur.children.get(part) ?? null;
      if (!cur) return null;
    }
    return cur;
  }

  private filetypeOf(node: Node) {
    return node.type === 'dir' ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE;
  }

  private writeFilestat(ptr: number, node: Node) {
    const v = this.view;
    v.setBigUint64(ptr + 0, 1n, true);                                  // dev
    v.setBigUint64(ptr + 8, 1n, true);                                  // ino
    v.setUint8(ptr + 16, this.filetypeOf(node));                        // filetype
    v.setBigUint64(ptr + 24, 1n, true);                                 // nlink
    v.setBigUint64(ptr + 32,
      BigInt(node.type === 'file' ? node.data.length : 0), true);       // size
    v.setBigUint64(ptr + 40, 0n, true);                                 // atim
    v.setBigUint64(ptr + 48, 0n, true);                                 // mtim
    v.setBigUint64(ptr + 56, 0n, true);                                 // ctim
    return ERR.SUCCESS;
  }

  get imports(): WebAssembly.ModuleImports {
    const rofs = () => ERR.ROFS;
    const notsup = () => ERR.NOTSUP;

    return {
      environ_sizes_get: (countPtr: number, sizePtr: number) => {
        const v = this.view;
        v.setUint32(countPtr, 0, true);
        v.setUint32(sizePtr, 0, true);
        return ERR.SUCCESS;
      },
      environ_get: () => ERR.SUCCESS,

      clock_res_get: (_id: number, ptr: number) => {
        this.view.setBigUint64(ptr, 1_000_000n, true);
        return ERR.SUCCESS;
      },
      // Deliberately coarse (milliseconds): the image needs a clock only for
      // stdlib bookkeeping, and a fine one is a timing side channel.
      clock_time_get: (_id: number, _prec: bigint, ptr: number) => {
        this.view.setBigUint64(ptr, BigInt(Date.now()) * 1_000_000n, true);
        return ERR.SUCCESS;
      },

      fd_advise: () => ERR.SUCCESS,
      fd_datasync: () => ERR.SUCCESS,
      fd_sync: () => ERR.SUCCESS,
      fd_fdstat_set_flags: () => ERR.SUCCESS,
      fd_filestat_set_size: rofs,
      fd_filestat_set_times: rofs,
      fd_pwrite: rofs,

      fd_close: (fd: number) => (this.fds.delete(fd) ? ERR.SUCCESS : ERR.BADF),

      fd_fdstat_get: (fd: number, ptr: number) => {
        const e = this.fds.get(fd);
        if (!e) return ERR.BADF;
        const v = this.view;
        v.setUint8(ptr, e.kind === 'dir' ? FILETYPE.DIRECTORY
          : e.kind === 'file' ? FILETYPE.REGULAR_FILE : FILETYPE.CHARACTER_DEVICE);
        v.setUint16(ptr + 2, 0, true);
        v.setBigUint64(ptr + 8, 0xFFFFFFFFFFFFFFFFn, true);
        v.setBigUint64(ptr + 16, 0xFFFFFFFFFFFFFFFFn, true);
        return ERR.SUCCESS;
      },

      fd_filestat_get: (fd: number, ptr: number) => {
        const e = this.fds.get(fd);
        if (!e) return ERR.BADF;
        if (e.kind === 'dir' || e.kind === 'file') return this.writeFilestat(ptr, e.node);
        const v = this.view;
        for (let off = 0; off < 64; off += 8) v.setBigUint64(ptr + off, 0n, true);
        v.setUint8(ptr + 16, FILETYPE.CHARACTER_DEVICE);
        return ERR.SUCCESS;
      },

      fd_prestat_get: (fd: number, ptr: number) => {
        if (fd !== 3) return ERR.BADF;
        const v = this.view;
        v.setUint8(ptr, 0);                       // preopentype: directory
        v.setUint32(ptr + 4, 4, true);            // strlen("/Lib")
        return ERR.SUCCESS;
      },

      fd_prestat_dir_name: (fd: number, ptr: number, len: number) => {
        if (fd !== 3) return ERR.BADF;
        const name = new TextEncoder().encode('/Lib');
        if (len < name.length) return ERR.INVAL;
        this.bytes.set(name, ptr);
        return ERR.SUCCESS;
      },

      fd_read: (fd: number, iovs: number, iovsLen: number, nreadPtr: number) => {
        const e = this.fds.get(fd);
        if (!e) return ERR.BADF;
        if (e.kind !== 'file') { this.view.setUint32(nreadPtr, 0, true); return ERR.SUCCESS; }
        const read = this.scatter(e.node.data, e.pos, iovs, iovsLen);
        e.pos += read;
        this.view.setUint32(nreadPtr, read, true);
        return ERR.SUCCESS;
      },

      fd_pread: (fd: number, iovs: number, iovsLen: number, offset: bigint, nreadPtr: number) => {
        const e = this.fds.get(fd);
        if (!e || e.kind !== 'file') return ERR.BADF;
        const read = this.scatter(e.node.data, Number(offset), iovs, iovsLen);
        this.view.setUint32(nreadPtr, read, true);
        return ERR.SUCCESS;
      },

      fd_seek: (fd: number, offset: bigint, whence: number, outPtr: number) => {
        const e = this.fds.get(fd);
        if (!e) return ERR.BADF;
        if (e.kind !== 'file') return ERR.SPIPE;
        const off = Number(offset);
        const size = e.node.data.length;
        const pos = whence === 0 ? off : whence === 1 ? e.pos + off : size + off;
        if (pos < 0) return ERR.INVAL;
        e.pos = pos;
        this.view.setBigUint64(outPtr, BigInt(pos), true);
        return ERR.SUCCESS;
      },

      fd_tell: (fd: number, ptr: number) => {
        const e = this.fds.get(fd);
        if (!e || e.kind !== 'file') return ERR.BADF;
        this.view.setBigUint64(ptr, BigInt(e.pos), true);
        return ERR.SUCCESS;
      },

      // CPython's FileFinder lists a directory once and caches it, so this is
      // called a handful of times at startup and then never again.
      fd_readdir: (fd: number, buf: number, bufLen: number, cookie: bigint, usedPtr: number) => {
        const e = this.fds.get(fd);
        if (!e || e.kind !== 'dir') return ERR.BADF;
        const entries = [...(e.node as any).children.entries()] as [string, Node][];
        const enc = new TextEncoder();
        const v = this.view;
        let off = 0;
        for (let i = Number(cookie); i < entries.length; i++) {
          const [name, node] = entries[i];
          const nb = enc.encode(name);
          if (off + 24 + nb.length > bufLen) break;
          v.setBigUint64(buf + off + 0, BigInt(i + 1), true);   // d_next
          v.setBigUint64(buf + off + 8, BigInt(i + 1), true);   // d_ino
          v.setUint32(buf + off + 16, nb.length, true);         // d_namlen
          v.setUint8(buf + off + 20, this.filetypeOf(node));    // d_type
          this.bytes.set(nb, buf + off + 24);
          off += 24 + nb.length;
        }
        v.setUint32(usedPtr, off, true);
        return ERR.SUCCESS;
      },

      fd_write: (fd: number, iovs: number, iovsLen: number, nwrittenPtr: number) => {
        const e = this.fds.get(fd);
        if (!e) return ERR.BADF;
        if (e.kind !== 'stream') return ERR.ROFS;
        const v = this.view;
        let written = 0;
        let text = '';
        for (let i = 0; i < iovsLen; i++) {
          const ptr = v.getUint32(iovs + i * 8, true);
          const len = v.getUint32(iovs + i * 8 + 4, true);
          text += new TextDecoder().decode(this.bytes.subarray(ptr, ptr + len));
          written += len;
        }
        e.partial += text;
        const lines = e.partial.split('\n');
        e.partial = lines.pop()!;
        const sink = e.which === 'out' ? this.onStdout : this.onStderr;
        for (const line of lines) sink(line);
        v.setUint32(nwrittenPtr, written, true);
        return ERR.SUCCESS;
      },

      path_open: (dirFd: number, _dirFlags: number, pathPtr: number, pathLen: number,
                  oflags: number, _base: bigint, _inh: bigint, _fdFlags: number,
                  openedPtr: number) => {
        const dir = this.fds.get(dirFd);
        if (!dir || dir.kind !== 'dir') return ERR.BADF;
        const path = this.str(pathPtr, pathLen);
        this.opens.push(path);
        if (oflags & (O_CREAT | O_TRUNC | O_EXCL)) return ERR.ROFS;
        const node = this.resolve(dir.node, path);
        if (!node) return ERR.NOENT;
        if ((oflags & O_DIRECTORY) && node.type !== 'dir') return ERR.NOTDIR;
        const fd = this.nextFd++;
        this.fds.set(fd, node.type === 'dir'
          ? { kind: 'dir', node }
          : { kind: 'file', node, pos: 0 });
        this.view.setUint32(openedPtr, fd, true);
        return ERR.SUCCESS;
      },

      path_filestat_get: (dirFd: number, _flags: number, pathPtr: number,
                          pathLen: number, bufPtr: number) => {
        const dir = this.fds.get(dirFd);
        if (!dir || dir.kind !== 'dir') return ERR.BADF;
        const node = this.resolve(dir.node, this.str(pathPtr, pathLen));
        if (!node) return ERR.NOENT;
        return this.writeFilestat(bufPtr, node);
      },

      path_readlink: () => ERR.INVAL,
      path_create_directory: rofs,
      path_filestat_set_times: rofs,
      path_link: rofs,
      path_remove_directory: rofs,
      path_rename: rofs,
      path_symlink: rofs,
      path_unlink_file: rofs,

      poll_oneoff: () => ERR.NOSYS,
      sched_yield: () => ERR.SUCCESS,
      random_get: (ptr: number, len: number) => {
        crypto.getRandomValues(this.bytes.subarray(ptr, ptr + len));
        return ERR.SUCCESS;
      },
      proc_exit: (code: number) => {
        throw new Error('sandbox image called proc_exit(' + code + ')');
      },

      // No network exists here, by construction.
      sock_accept: notsup,
      sock_recv: notsup,
      sock_send: notsup,
      sock_shutdown: notsup,
    };
  }

  private scatter(data: Uint8Array, pos: number, iovs: number, iovsLen: number) {
    const v = this.view;
    let read = 0;
    for (let i = 0; i < iovsLen; i++) {
      const ptr = v.getUint32(iovs + i * 8, true);
      const len = v.getUint32(iovs + i * 8 + 4, true);
      const chunk = data.subarray(pos + read, pos + read + len);
      this.bytes.set(chunk, ptr);
      read += chunk.length;
      if (chunk.length < len) break;
    }
    return read;
  }
}
