# qcode systemd user units

These are the units installed on this machine at `~/.config/systemd/user/`:

| File | Installed unit | Purpose |
|------|----------------|---------|
| `qcode-server.service` | `qcode-server.service` | Runs the installed `/usr/sbin/qcode serve --host 0.0.0.0 --port 9080` |
| `qcode-scratch-cleanup.service` | `qcode-scratch-cleanup.service` | Runs `scripts/clean_scratch.sh` |
| `qcode-scratch-cleanup.timer` | `qcode-scratch-cleanup.timer` | Hourly trigger for the cleanup |

Install or update them with:

```sh
cp scripts/systemd/*.service scripts/systemd/*.timer ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now qcode-server.service qcode-scratch-cleanup.timer
```

`qcode-server.sh` (in `scripts/`) starts, stops, restarts and status-checks the
server. The unit restarts itself after any stop that was not deliberate, so a
stopped server does not stay down; `qcode-server.sh stop` writes the marker that
tells it the stop is intentional.

`deploy_server.sh` installs a build and restarts the server from a detached
transient unit, so it also works when run from inside the server itself.
