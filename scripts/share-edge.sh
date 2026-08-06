#!/bin/bash
# Put an authenticating (or plain) front door in front of a sharing
# backend (docs/ShareAccess.md §4/§5). The origin-side contract is the
# same whichever door runs: it connects from loopback, forwards the
# client address (X-Forwarded-For / CF-Connecting-IP), and — when it
# authenticates — asserts the verified identity in a header the scene
# server recognizes out of the box. So the FreeCAD side is always just:
#
#   FC_SERVE_TRUST_PROXY=1   believe those headers from loopback peers
#   FC_SERVE_TOKEN=...       optional bearer door on top (or instead)
#
# Modes:
#   quick  [port]             cloudflared quick tunnel (trycloudflare.com).
#                             No account, no auth — the link itself (plus
#                             the token) is the whole door. Sharing with
#                             people you trust, right now.
#   access <hostname> [port]  cloudflared named tunnel meant to sit behind
#                             Cloudflare Access. Access logs the viewer in
#                             (Google/GitHub/email PIN) and asserts
#                             Cf-Access-Authenticated-User-Email; free for
#                             up to 50 users. Prints the one-time dashboard
#                             setup on first use.
#   caddy  <hostname> [port]  self-hosted door: generate a Caddyfile +
#                             oauth2-proxy invocation giving the identical
#                             contract (X-Auth-Request-Email) with only
#                             your own box in the path — for when even the
#                             triangles must stay yours.
#
# The tunnel fronts the serve port itself: the backend serves the viewer
# page too (FC_BGFX_VIEWER_BUILD), so one hostname carries page, scene
# stream and blobs alike. Keepalive pings keep a parked viewer alive
# through Cloudflare's 100 s idle cutoff.
#
# Usage:  scripts/share-edge.sh quick [8077]
#         scripts/share-edge.sh access cad.example.com [8077]
#         scripts/share-edge.sh caddy cad.example.com [8077]
set -u
MODE=${1:-}
usage() { sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

need() {
    command -v "$1" >/dev/null && return
    echo "missing: $1"
    echo "  $2"
    exit 2
}

case "$MODE" in
quick)
    PORT=${2:-8077}
    need cloudflared "install: https://developers.cloudflare.com/cloudflared/ (or: conda install cloudflared)"
    echo "backend side (if not already up):"
    echo "  FC_SERVE_TRUST_PROXY=1 FC_SERVE_TOKEN=<secret> scripts/renderer-serve.sh <scene.py> $PORT"
    echo "tunnel starting; share the https://….trycloudflare.com link it prints"
    echo "(with your ?token=… tail — the quick tunnel itself checks nobody)."
    exec cloudflared tunnel --url "http://localhost:$PORT"
    ;;
access)
    HOST=${2:-}; PORT=${3:-8077}
    [ -n "$HOST" ] || usage
    need cloudflared "install: https://developers.cloudflare.com/cloudflared/"
    TUNNEL=${FC_EDGE_TUNNEL:-fc-share}
    if ! cloudflared tunnel list 2>/dev/null | grep -q "[[:space:]]$TUNNEL[[:space:]]"; then
        echo "one-time setup for tunnel '$TUNNEL' -> $HOST:"
        echo "  cloudflared tunnel login"
        echo "  cloudflared tunnel create $TUNNEL"
        echo "  cloudflared tunnel route dns $TUNNEL $HOST"
        echo "then, in the Zero Trust dashboard (one.dash.cloudflare.com):"
        echo "  Access -> Applications -> Add -> Self-hosted, domain $HOST"
        echo "  add an Allow policy (emails / email domain / everyone-with-PIN)"
        echo "and rerun this command. The origin then sees each viewer's"
        echo "verified email in Cf-Access-Authenticated-User-Email; probes"
        echo "keep working via Access service tokens (or keep FC_SERVE_TOKEN"
        echo "as the machine door)."
        exit 2
    fi
    echo "backend side (if not already up):"
    echo "  FC_SERVE_TRUST_PROXY=1 scripts/renderer-serve.sh <scene.py> $PORT"
    echo "fronting http://localhost:$PORT as https://$HOST (tunnel $TUNNEL)"
    exec cloudflared tunnel run --url "http://localhost:$PORT" "$TUNNEL"
    ;;
caddy)
    HOST=${2:-}; PORT=${3:-8077}
    [ -n "$HOST" ] || usage
    OUT=${FC_EDGE_DIR:-$HOME/.config/fc-share-edge}
    mkdir -p "$OUT"
    cat > "$OUT/Caddyfile" <<EOF
# Self-hosted sharing front door (docs/ShareAccess.md §4): Caddy
# terminates TLS on your own box and asks oauth2-proxy who the viewer
# is; the verified email reaches the origin in X-Auth-Request-Email,
# which the scene server recognizes when FC_SERVE_TRUST_PROXY=1.
$HOST {
    forward_auth 127.0.0.1:4180 {
        uri /oauth2/auth
        copy_headers X-Auth-Request-Email
    }
    # oauth2-proxy's own login/callback pages bypass the auth check.
    handle /oauth2/* {
        reverse_proxy 127.0.0.1:4180
    }
    reverse_proxy 127.0.0.1:$PORT
}
EOF
    cat > "$OUT/oauth2-proxy.env" <<'EOF'
# Fill in and source before launching oauth2-proxy (or use its own
# --flags). Any provider oauth2-proxy speaks works the same way;
# Google shown. Redirect URL to register with the provider:
#   https://<hostname>/oauth2/callback
OAUTH2_PROXY_PROVIDER=google
OAUTH2_PROXY_CLIENT_ID=
OAUTH2_PROXY_CLIENT_SECRET=
OAUTH2_PROXY_COOKIE_SECRET=   # python3 -c 'import os,base64;print(base64.urlsafe_b64encode(os.urandom(32)).decode())'
OAUTH2_PROXY_EMAIL_DOMAINS=*  # or your domain: who may log in at all
OAUTH2_PROXY_HTTP_ADDRESS=127.0.0.1:4180
OAUTH2_PROXY_REVERSE_PROXY=true
OAUTH2_PROXY_SET_XAUTHREQUEST=true
OAUTH2_PROXY_UPSTREAMS=static://202
EOF
    echo "wrote $OUT/Caddyfile and $OUT/oauth2-proxy.env"
    echo "run (on the box that owns $HOST):"
    echo "  oauth2-proxy  # with the env above filled in and sourced"
    echo "  caddy run --config $OUT/Caddyfile"
    echo "backend side:"
    echo "  FC_SERVE_TRUST_PROXY=1 scripts/renderer-serve.sh <scene.py> $PORT"
    command -v caddy >/dev/null || echo "note: caddy not installed here (https://caddyserver.com/docs/install)"
    command -v oauth2-proxy >/dev/null || echo "note: oauth2-proxy not installed here (https://oauth2-proxy.github.io/oauth2-proxy/)"
    ;;
*)
    usage
    ;;
esac
