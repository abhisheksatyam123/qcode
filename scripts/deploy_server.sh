#!/usr/bin/env bash
#
# Install a qcode build and restart the qcode server service.
#
# This script is safe to run from inside the server itself. The WebUI bash
# tool, `qcode prompt`, and every qcode session run inside the
# qcode-server.service cgroup, so a plain
#
#     systemctl --user stop qcode-server.service
#
# issued from such a call SIGTERMs the server that is running the call. The
# server aborts its tool calls and exits, so the rest of the command -- the
# binary copy and the restart -- never runs and the server stays down. That is
# exactly how the server was left stopped on 2026-11-10 22:04:29, when a
# deploy run from a WebUI session killed itself before it could restart.
#
# Here the stop/copy/start sequence runs in a detached systemd transient unit
# (a sibling of the server unit, outside the server's cgroup), so it still
# runs after the server tears down the calling tool. The new binary is
# health-checked and rolled back if the server does not come back up.
#
# Usage:
#   scripts/deploy_server.sh [--binary PATH] [--install PATH] [--service NAME]
#                            [--port N] [--host ADDR] [--backup-dir DIR]
#                            [--no-wait]
#
# Examples:
#   scripts/deploy_server.sh
#   scripts/deploy_server.sh --binary build/host-release/apps/qcode/qcode
#   scripts/deploy_server.sh --binary /tmp/qcode.new --port 9081

set -euo pipefail

SERVICE="qcode-server.service"
INSTALL_PATH="/usr/sbin/qcode"
BINARY=""
PORT="9080"
HOST="127.0.0.1"
BACKUP_DIR="${HOME}/tmp"
NO_WAIT=0

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --binary) BINARY="${2:?--binary needs a path}"; shift 2 ;;
        --install) INSTALL_PATH="${2:?--install needs a path}"; shift 2 ;;
        --service) SERVICE="${2:?--service needs a name}"; shift 2 ;;
        --port) PORT="${2:?--port needs a number}"; shift 2 ;;
        --host) HOST="${2:?--host needs an address}"; shift 2 ;;
        --backup-dir) BACKUP_DIR="${2:?--backup-dir needs a path}"; shift 2 ;;
        --no-wait) NO_WAIT=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "$BINARY" ]; then
    BINARY="${REPO_ROOT}/build/host-release/apps/qcode/qcode"
fi
BINARY="$(cd "$(dirname "$BINARY")" && pwd)/$(basename "$BINARY")"

if [ ! -x "$BINARY" ]; then
    echo "Error: no executable at $BINARY" >&2
    echo "Build one first: uv run scripts/build.py --mode release" >&2
    exit 1
fi

mkdir -p "$BACKUP_DIR"
RUN_DIR="$(mktemp -d "${BACKUP_DIR}/qcode-deploy.XXXXXX")"
WORKER="${RUN_DIR}/worker.sh"
LOG_FILE="${RUN_DIR}/deploy.log"
UNIT="qcode-deploy-$(date +%s)-$$"
# The unit restarts itself after an unintentional stop; the marker tells it that
# this stop is deliberate, so the deploy controls when the server comes back.
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
MARKER="${RUNTIME_DIR}/qcode-server.stopped"

VERSION="$("$BINARY" --version 2>/dev/null | head -1 || echo "unknown")"
BACKUP="${RUN_DIR}/$(basename "$INSTALL_PATH").before.bak"

# The worker runs as its own systemd unit, outside the server's cgroup, so it
# survives the server shutting itself down.
cat >"$WORKER" <<EOF
#!/usr/bin/env bash
set -uo pipefail
exec >>"$LOG_FILE" 2>&1
echo "=== qcode deploy \$(date -Is) ==="
echo "installing: $BINARY"
echo "target:     $INSTALL_PATH"
echo "service:    $SERVICE"
echo "version:    $VERSION"

fail() {
    echo "RESULT: failed (\$1)"
    exit 1
}

# cp writes the file in place; install(1) renames and would need a writable
# directory (/usr/sbin is root-owned, only the binary itself is ours).
copy_binary() {
    cp -f "\$1" "$INSTALL_PATH" || return 1
    chmod 0755 "$INSTALL_PATH"
}

# Back up the running binary before touching it.
if ! cp -p "$INSTALL_PATH" "$BACKUP"; then
    echo "could not back up $INSTALL_PATH"
    fail "backup"
fi

# Stop the old server, then swap the binary in. The marker keeps the unit from
# scheduling its own restart while the new binary is being installed.
touch "$MARKER"
systemctl --user stop "$SERVICE" || true
for _ in \$(seq 1 60); do
    systemctl --user is-active --quiet "$SERVICE" || break
    sleep 0.5
done
if ! copy_binary "$BINARY"; then
    echo "install failed, restoring the previous binary"
    copy_binary "$BACKUP" || true
    rm -f "$MARKER"
    systemctl --user start "$SERVICE" || true
    fail "install"
fi

rm -f "$MARKER"
systemctl --user start "$SERVICE" || fail "start"

# Health-check the new server; roll back if it does not come up.
healthy=0
for _ in \$(seq 1 60); do
    if curl -fsS --max-time 3 "http://$HOST:$PORT/health" >/dev/null 2>&1; then
        healthy=1
        break
    fi
    sleep 1
done
if [ "\$healthy" -ne 1 ]; then
    echo "health check failed, rolling back to $BACKUP"
    systemctl --user stop "$SERVICE" || true
    copy_binary "$BACKUP" || true
    rm -f "$MARKER"
    systemctl --user start "$SERVICE" || true
    fail "health"
fi

echo "installed: \$($INSTALL_PATH --version 2>/dev/null | head -1)"
echo "backup:    $BACKUP"
echo "RESULT: ok"
EOF
chmod +x "$WORKER"

echo "deploying $VERSION -> $INSTALL_PATH"
echo "log: $LOG_FILE"

systemd-run --user --unit="$UNIT" --property=TimeoutStartSec=300 /bin/bash "$WORKER" >/dev/null

if [ "$NO_WAIT" -eq 1 ]; then
    echo "started as unit $UNIT; log: $LOG_FILE"
    exit 0
fi

# Wait for the worker to report. If this process is killed (the server shut
# itself down mid-deploy), the worker still finishes on its own.
deadline=$((SECONDS + 300))
while [ $SECONDS -lt $deadline ]; do
    if ! systemctl --user is-active --quiet "$UNIT"; then
        break
    fi
    sleep 1
done

result="$(grep -E '^RESULT:' "$LOG_FILE" 2>/dev/null | tail -1 || true)"
if [ -z "$result" ]; then
    echo "deploy did not report a result; unit $UNIT log:"
    cat "$LOG_FILE" 2>/dev/null || true
    exit 1
fi

sed 's/^/  /' "$LOG_FILE" >&2
if [ "$result" != "RESULT: ok" ]; then
    echo "deploy FAILED: $result" >&2
    exit 1
fi
echo "deploy OK"
