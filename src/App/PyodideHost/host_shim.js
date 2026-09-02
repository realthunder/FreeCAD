// The Web environment pyodide's emscripten glue expects, authored here on
// purpose.  A bare V8 context has ECMAScript and WebAssembly and nothing
// else; everything below is added by hand, and what is NOT added is the
// security mechanism: there is no fetch, no filesystem, no process, no
// module loader beyond the scoped one the host installs.  See
// docs/PyodideHost.md.
//
// The host (PyodideHost.cc) installs a few native functions on
// `__fcx_host` before running this file:
//   readText(path) -> string          only under the pyodide root
//   readBytes(path) -> ArrayBuffer    only under the pyodide root
//   randomBytes(n) -> ArrayBuffer     from the host RNG
//   now() -> double ms                monotonic
//   print(kind, text)                 kind is "out" or "err"
// This file turns those into the shape pyodide reads, and removes
// `__fcx_host` from the global when it is done.
(function (host) {
  "use strict";

  // ---------------------------------------------------------------- console
  function fmt(args) {
    return Array.prototype.map.call(args, function (a) {
      if (typeof a === "string") return a;
      if (a instanceof Error) return a.stack || String(a);
      try { return JSON.stringify(a); } catch (e) { return String(a); }
    }).join(" ");
  }
  var console = {
    log: function () { host.print("out", fmt(arguments)); },
    info: function () { host.print("out", fmt(arguments)); },
    debug: function () { host.print("out", fmt(arguments)); },
    warn: function () { host.print("err", fmt(arguments)); },
    error: function () { host.print("err", fmt(arguments)); },
    trace: function () { host.print("err", fmt(arguments)); },
  };
  globalThis.console = console;
  // d8 names, which the emscripten shell branch wires console onto.
  globalThis.print = function () { host.print("out", fmt(arguments)); };
  globalThis.printErr = function () { host.print("err", fmt(arguments)); };

  // ------------------------------------------------------------- d8 readers
  // `typeof read == "function" && typeof load == "function"` is how
  // pyodide.js decides it is in a shell; the emscripten glue then reads
  // with read(f, "binary") or readbuffer(f).  All three go through the
  // host, which refuses any path outside the pyodide root.
  globalThis.read = function (path, mode) {
    if (mode === "binary") return new Uint8Array(host.readBytes(String(path)));
    return host.readText(String(path));
  };
  globalThis.readbuffer = function (path) {
    return host.readBytes(String(path));
  };
  // Only detection needs load(); nothing is ever loaded through it.
  globalThis.load = function () {
    throw new Error("load() is not available in this host");
  };

  // ------------------------------------------------------------ random bytes
  // In the shell branch emscripten's initRandomFill does not look for
  // crypto at all: it runs `head -c N /dev/urandom | base64` through
  // os.system.  This os.system answers exactly that request from the host
  // RNG and nothing else.  crypto.getRandomValues is provided too for the
  // non-shell code paths (pyodide's own ffi layer, Python's os.urandom
  // after startup).
  function b64(bytes) {
    var A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var out = "", i = 0, n = bytes.length;
    for (; i + 2 < n; i += 3) {
      var v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
      out += A[v >> 18] + A[(v >> 12) & 63] + A[(v >> 6) & 63] + A[v & 63];
    }
    if (i < n) {
      var v2 = bytes[i] << 16 | ((i + 1 < n ? bytes[i + 1] : 0) << 8);
      out += A[v2 >> 18] + A[(v2 >> 12) & 63];
      out += (i + 1 < n) ? A[(v2 >> 6) & 63] : "=";
      out += "=";
    }
    return out;
  }
  var urandom = /^head -c(\d+) \/dev\/urandom \| base64 --wrap=0$/;
  globalThis.os = {
    system: function (cmd, args) {
      var m = (cmd === "sh" && args && args.length === 2 && args[0] === "-c")
        ? urandom.exec(args[1]) : null;
      if (!m) throw new Error("os.system: refused: " + cmd + " " + JSON.stringify(args));
      var n = Number(m[1]);
      if (!(n >= 0 && n <= 65536)) throw new Error("os.system: refused size " + n);
      return b64(new Uint8Array(host.randomBytes(n)));
    },
  };
  globalThis.crypto = {
    getRandomValues: function (view) {
      if (!ArrayBuffer.isView(view)) throw new TypeError("getRandomValues: not a view");
      var bytes = new Uint8Array(host.randomBytes(view.byteLength));
      new Uint8Array(view.buffer, view.byteOffset, view.byteLength).set(bytes);
      return view;
    },
    randomUUID: function () {
      var b = new Uint8Array(host.randomBytes(16));
      b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80;
      var h = Array.prototype.map.call(b, function (x) { return (x < 16 ? "0" : "") + x.toString(16); }).join("");
      return h.slice(0, 8) + "-" + h.slice(8, 12) + "-" + h.slice(12, 16) + "-" + h.slice(16, 20) + "-" + h.slice(20);
    },
  };

  // ------------------------------------------------------------- performance
  globalThis.performance = {
    now: function () { return host.now(); },
    timeOrigin: 0,
  };

  // ------------------------------------------------------------------ timers
  // A real timer queue rather than emscripten's shell fallback
  // (setTimeout = f => f(), synchronous).  The host drives it: after each
  // microtask checkpoint it calls __fcx_runTimers(), which fires what is
  // due and returns the ms until the next one, or -1 when idle.
  var timers = new Map(), nextTimerId = 1;
  function addTimer(fn, delay, repeat, args) {
    var id = nextTimerId++;
    var d = Math.max(0, Number(delay) || 0);
    timers.set(id, { fn: fn, args: args, when: host.now() + d, period: repeat ? d : -1 });
    return id;
  }
  globalThis.setTimeout = function (fn, delay) {
    return addTimer(fn, delay, false, Array.prototype.slice.call(arguments, 2));
  };
  globalThis.setInterval = function (fn, delay) {
    return addTimer(fn, delay, true, Array.prototype.slice.call(arguments, 2));
  };
  globalThis.clearTimeout = globalThis.clearInterval = function (id) { timers.delete(id); };
  globalThis.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  globalThis.__fcx_runTimers = function () {
    var now = host.now(), due = [];
    timers.forEach(function (t, id) { if (t.when <= now) due.push(id); });
    due.sort(function (a, b) { return timers.get(a).when - timers.get(b).when; });
    for (var i = 0; i < due.length; i++) {
      var t = timers.get(due[i]);
      if (!t) continue;
      if (t.period >= 0) t.when = now + t.period; else timers.delete(due[i]);
      if (typeof t.fn === "function") t.fn.apply(undefined, t.args);
    }
    var next = -1;
    timers.forEach(function (t) {
      var wait = Math.max(0, t.when - host.now());
      next = next < 0 ? wait : Math.min(next, wait);
    });
    return next;
  };

  // ------------------------------------------------------- TextEncoder/Decoder
  // UTF-8 both ways plus UTF-16LE and Latin-1 decoding, which is what the
  // glue asks for (UTF8ToString, UTF16ToString, Python's js decode).
  function TextEncoder() {}
  TextEncoder.prototype.encoding = "utf-8";
  TextEncoder.prototype.encode = function (s) {
    s = s === undefined ? "" : String(s);
    var out = [], i = 0;
    while (i < s.length) {
      var c = s.codePointAt(i);
      i += c > 0xffff ? 2 : 1;
      if (c < 0x80) out.push(c);
      else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
      else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
      else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return new Uint8Array(out);
  };
  TextEncoder.prototype.encodeInto = function (s, dest) {
    var b = this.encode(s), n = Math.min(b.length, dest.length);
    dest.set(b.subarray(0, n));
    return { read: s.length, written: n };
  };
  function normEncoding(label) {
    var l = String(label === undefined ? "utf-8" : label).trim().toLowerCase();
    if (l === "utf-8" || l === "utf8" || l === "unicode-1-1-utf-8") return "utf-8";
    if (l === "utf-16le" || l === "utf-16" || l === "ucs-2") return "utf-16le";
    if (l === "latin1" || l === "iso-8859-1" || l === "ascii" || l === "us-ascii" || l === "windows-1252") return "latin1";
    throw new RangeError("TextDecoder: unsupported encoding " + label);
  }
  function TextDecoder(label, options) {
    this.encoding = normEncoding(label);
    this.fatal = !!(options && options.fatal);
    this.ignoreBOM = !!(options && options.ignoreBOM);
  }
  function toBytes(input) {
    if (input === undefined) return new Uint8Array(0);
    if (input instanceof ArrayBuffer) return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    throw new TypeError("TextDecoder.decode: not a BufferSource");
  }
  TextDecoder.prototype.decode = function (input) {
    var b = toBytes(input), out = "", i = 0, n = b.length;
    if (this.encoding === "latin1") {
      for (; i < n; i++) out += String.fromCharCode(b[i]);
      return out;
    }
    if (this.encoding === "utf-16le") {
      if (!this.ignoreBOM && n >= 2 && b[0] === 0xff && b[1] === 0xfe) i = 2;
      var cu = [];
      for (; i + 1 < n; i += 2) cu.push(b[i] | (b[i + 1] << 8));
      if (i < n && this.fatal) throw new TypeError("TextDecoder: truncated utf-16 input");
      for (var j = 0; j < cu.length; j += 4096) out += String.fromCharCode.apply(null, cu.slice(j, j + 4096));
      return out;
    }
    if (!this.ignoreBOM && n >= 3 && b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) i = 3;
    var cps = [];
    while (i < n) {
      var c = b[i++], cp, need = 0, min = 0;
      if (c < 0x80) { cps.push(c); continue; }
      else if ((c & 0xe0) === 0xc0) { cp = c & 0x1f; need = 1; min = 0x80; }
      else if ((c & 0xf0) === 0xe0) { cp = c & 0x0f; need = 2; min = 0x800; }
      else if ((c & 0xf8) === 0xf0) { cp = c & 0x07; need = 3; min = 0x10000; }
      else { if (this.fatal) throw new TypeError("TextDecoder: invalid utf-8"); cps.push(0xfffd); continue; }
      var ok = true;
      for (var k = 0; k < need; k++) {
        var cc = i < n ? b[i] : -1;
        if ((cc & 0xc0) !== 0x80) { ok = false; break; }
        cp = (cp << 6) | (cc & 63); i++;
      }
      if (!ok || cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        if (this.fatal) throw new TypeError("TextDecoder: invalid utf-8");
        cps.push(0xfffd); continue;
      }
      cps.push(cp);
    }
    for (var m = 0; m < cps.length; m += 2048) out += String.fromCodePoint.apply(null, cps.slice(m, m + 2048));
    return out;
  };
  globalThis.TextEncoder = TextEncoder;
  globalThis.TextDecoder = TextDecoder;

  // ------------------------------------------------------------------- URL
  // Enough of WHATWG URL for path resolution against file: and
  // relative bases, which is what the loaders do with it.  No network
  // meaning is attached to any scheme here; a URL is a string.
  function URL(url, base) {
    url = String(url);
    var m = /^([a-zA-Z][a-zA-Z0-9+.-]*):(.*)$/.exec(url);
    if (m) { this.protocol = m[1].toLowerCase() + ":"; this.pathname = m[2]; }
    else {
      if (base === undefined) throw new TypeError("URL: relative URL without a base");
      var b = base instanceof URL ? base : new URL(String(base));
      this.protocol = b.protocol;
      var dir = b.pathname.replace(/[^\/]*$/, "");
      var p = url.charAt(0) === "/" ? url : dir + url;
      var parts = [], segs = p.split("/");
      for (var i = 0; i < segs.length; i++) {
        if (segs[i] === "..") parts.pop();
        else if (segs[i] !== "." && (segs[i] !== "" || i === 0 || i === segs.length - 1)) parts.push(segs[i]);
      }
      this.pathname = parts.join("/");
    }
    this.href = this.protocol + this.pathname;
    this.origin = "null"; this.host = ""; this.hostname = ""; this.port = "";
    this.search = ""; this.hash = "";
  }
  URL.prototype.toString = function () { return this.href; };
  URL.prototype.toJSON = function () { return this.href; };
  URL.createObjectURL = function () { throw new Error("URL.createObjectURL is not available in this host"); };
  globalThis.URL = URL;

  // -------------------------------------------------------------- base64
  // pyodide's package loader base64-encodes wheel digests.
  var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  globalThis.btoa = function (s) {
    s = String(s);
    var bytes = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c > 255) throw new Error("btoa: character out of Latin-1 range");
      bytes[i] = c;
    }
    return b64(bytes);
  };
  globalThis.atob = function (s) {
    s = String(s).replace(/[\s=]+$/g, "").replace(/\s+/g, "");
    var out = "", buf = 0, bits = 0;
    for (var i = 0; i < s.length; i++) {
      var v = B64.indexOf(s.charAt(i));
      if (v < 0) throw new Error("atob: invalid character");
      buf = (buf << 6) | v; bits += 6;
      if (bits >= 8) { bits -= 8; out += String.fromCharCode((buf >> bits) & 255); }
    }
    return out;
  };

  // ------------------------------------------------------------ housekeeping
  // Not a Web platform, but pyodide reads it in a few places.
  globalThis.self = globalThis;
  delete globalThis.__fcx_host;
})(globalThis.__fcx_host);
