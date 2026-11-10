#!/usr/bin/env bash
#
# Control the qcode web server (the systemd --user unit qcode-server.service).
#
# The unit restarts itself after any stop that was not asked for on purpose,
# so the web server does not silently stay down after an agent (or a crashed
# tool call) stops it. `stop` is the way to keep it down on purpose.
#
# Usage: scripts/qcode-server.sh {start|stop|restart|status}

set -euo pipefail

SERVICE="qcode-server.service"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
MARKER="${RUNTIME_DIR}/qcode-server.stopped"
PORT="9080"

case "${1:-status}" in
    start)
        rm -f "$MARKER"
        systemctl --user start "$SERVICE"
        ;;
    stop)
        # The marker tells the unit's ExecStopPost that this stop is deliberate.
        touch "$MARKER"
        systemctl --user stop "$SERVICE"
        ;;
    restart)
        rm -f "$MARKER"
        systemctl --user restart "$SERVICE"
        ;;
    status)
        systemctl --user status "$SERVICE" --no-pager || true
        echo
        if curl -fsS --max-time 5 "http://127.0.0.1:${2:-$PORT}/health"; then
            echo
        else
            echo "health check failed"
            exit 1
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}" >&2
        exit 2
        ;;
esac
