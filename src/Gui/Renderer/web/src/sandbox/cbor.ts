// CBOR, just far enough for the sandbox wire (src/App/ExpressionImage/FcxWire.h):
// the value set is null / bool / int / float / str / bytes / array / map with
// string keys.  nlohmann::json's to_cbor / from_cbor sit on the other end, so
// this only has to agree with that, not with all of RFC 8949.

export type Cbor =
  | null | boolean | number | bigint | string | Uint8Array
  | Cbor[] | { [key: string]: Cbor };

export function encode(value: Cbor): Uint8Array {
  const out: number[] = [];

  const head = (major: number, n: number) => {
    const mt = major << 5;
    if (n < 24) out.push(mt | n);
    else if (n < 0x100) out.push(mt | 24, n);
    else if (n < 0x10000) out.push(mt | 25, n >> 8, n & 0xff);
    else if (n < 0x1_0000_0000)
      out.push(mt | 26, (n >>> 24) & 0xff, (n >>> 16) & 0xff, (n >>> 8) & 0xff, n & 0xff);
    else {
      const big = BigInt(n);
      out.push(mt | 27);
      for (let s = 56n; s >= 0n; s -= 8n) out.push(Number((big >> s) & 0xffn));
    }
  };

  const enc = (v: Cbor): void => {
    if (v === null || v === undefined) { out.push(0xf6); return; }
    if (typeof v === 'boolean') { out.push(v ? 0xf5 : 0xf4); return; }
    if (typeof v === 'bigint') {
      if (v >= 0n) head(0, Number(v)); else head(1, Number(-v - 1n));
      return;
    }
    if (typeof v === 'number') {
      if (Number.isInteger(v) && Math.abs(v) <= Number.MAX_SAFE_INTEGER) {
        if (v >= 0) head(0, v); else head(1, -v - 1);
      } else {
        out.push(0xfb);
        const b = new Uint8Array(8);
        new DataView(b.buffer).setFloat64(0, v, false);
        for (const x of b) out.push(x);
      }
      return;
    }
    if (typeof v === 'string') {
      const b = new TextEncoder().encode(v);
      head(3, b.length);
      for (const x of b) out.push(x);
      return;
    }
    if (v instanceof Uint8Array) {
      head(2, v.length);
      for (const x of v) out.push(x);
      return;
    }
    if (Array.isArray(v)) { head(4, v.length); v.forEach(enc); return; }
    const keys = Object.keys(v);
    head(5, keys.length);
    for (const k of keys) { enc(k); enc(v[k]); }
  };

  enc(value);
  return new Uint8Array(out);
}

export function decode(bytes: Uint8Array): any {
  let i = 0;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

  const readN = (n: number) => {
    let v = 0n;
    for (let k = 0; k < n; k++) v = (v << 8n) | BigInt(bytes[i++]);
    return v;
  };

  const argOf = (ai: number): bigint => {
    if (ai < 24) return BigInt(ai);
    if (ai === 24) return BigInt(bytes[i++]);
    if (ai === 25) return readN(2);
    if (ai === 26) return readN(4);
    if (ai === 27) return readN(8);
    throw new Error('bad cbor additional info ' + ai);
  };

  // Stay in Number where it is lossless; the wire's integers are counts,
  // handle ids and small values, so this is the common case.
  const toNum = (b: bigint) => (b <= BigInt(Number.MAX_SAFE_INTEGER)
    && b >= BigInt(Number.MIN_SAFE_INTEGER) ? Number(b) : b);

  const dec = (): any => {
    const b = bytes[i++];
    const major = b >> 5, ai = b & 0x1f;
    switch (major) {
      case 0: return toNum(argOf(ai));
      case 1: return toNum(-1n - argOf(ai));
      case 2: {
        const n = Number(argOf(ai));
        const s = bytes.slice(i, i + n); i += n; return s;
      }
      case 3: {
        const n = Number(argOf(ai));
        const s = new TextDecoder().decode(bytes.subarray(i, i + n)); i += n; return s;
      }
      case 4: {
        const n = Number(argOf(ai));
        const a: any[] = [];
        for (let k = 0; k < n; k++) a.push(dec());
        return a;
      }
      case 5: {
        const n = Number(argOf(ai));
        const o: Record<string, any> = {};
        for (let k = 0; k < n; k++) { const key = dec(); o[key] = dec(); }
        return o;
      }
      case 7:
        if (ai === 20) return false;
        if (ai === 21) return true;
        if (ai === 22) return null;
        if (ai === 23) return undefined;
        if (ai === 25) return halfToFloat(Number(readN(2)));
        if (ai === 26) { const v = view.getFloat32(i, false); i += 4; return v; }
        if (ai === 27) { const v = view.getFloat64(i, false); i += 8; return v; }
        throw new Error('bad cbor simple value ' + ai);
      default: throw new Error('bad cbor major type ' + major);
    }
  };

  return dec();
}

function halfToFloat(h: number) {
  const s = (h & 0x8000) >> 15, e = (h & 0x7c00) >> 10, f = h & 0x03ff;
  if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
  if (e === 0x1f) return f ? NaN : (s ? -Infinity : Infinity);
  return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
}
