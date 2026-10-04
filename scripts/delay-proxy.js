// A TCP relay that adds a fixed round-trip time (docs/Sandbox.md 7.20 C5).
//
// Usage:  node scripts/delay-proxy.js <target-host:port> <rtt_ms> [listen_port]
//
// Every chunk is held rtt/2 in each direction, in order, so a request and its
// answer cost one RTT on top of the target's own time -- what a LAN or a tunnel
// adds to a bridge op, which is strictly one at a time.  It models latency only:
// no bandwidth cap, no loss, no jitter.  Listens on 127.0.0.1 (port 0 = any) and
// prints `listening <port>` once it does.  No sudo needed, unlike tc netem.
const net = require('net');

const [target, rttArg, listenArg] = process.argv.slice(2);
if (!target || rttArg === undefined) {
  console.error('usage: node delay-proxy.js <target-host:port> <rtt_ms> [listen_port]');
  process.exit(2);
}
const at = target.lastIndexOf(':');
const host = target.slice(0, at);
const port = +target.slice(at + 1);
const half = Math.max(0, Math.round(+rttArg / 2));

// Timers of one duration fire in the order they were set, which keeps each
// direction's bytes in order.
const later = (fn) => (half > 0 ? setTimeout(fn, half) : setImmediate(fn));

function relay(from, to) {
  from.on('data', (chunk) => later(() => { if (!to.destroyed) to.write(chunk); }));
  from.on('end', () => later(() => { if (!to.destroyed) to.end(); }));
  from.on('error', () => {});
  from.on('close', () => setTimeout(() => to.destroy(), half + 50));
}

const server = net.createServer((client) => {
  client.setNoDelay(true);
  const upstream = net.connect(port, host);
  upstream.setNoDelay(true);
  relay(client, upstream);
  relay(upstream, client);
});
server.listen(+(listenArg || 0), '127.0.0.1', () => {
  console.log('listening ' + server.address().port);
});
