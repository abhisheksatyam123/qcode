# qcode user guide

This guide covers the two agent modes, subagents, keyboard shortcuts, and
database maintenance. Setup and configuration are in the [README](../README.md).

## First run

qcode reads providers and models from one `opencode.json` (`$OPENCODE_CONFIG`,
else `~/.config/opencode/opencode.json`). `qcode --help` prints the path.
Add a provider with at least one model to that file. Its API key can go in the
file or come from the environment, for example `OPENAI_API_KEY`. Without a
provider, the TUI shows a setup screen with the same steps, and `qcode serve`
exits with an error that names the file.

`qcode prompt` needs a running server. Start one with `qcode serve`, or pass
`--server <url>`.

## Agent modes

qcode has two modes. The mode decides whether the agent can delegate work.

| Mode | What the agent does |
|------|---------------------|
| `normal` (default) | Works alone with bash, and with images when the model supports them. It has no `task` tool. |
| `orchestrator` | Splits the request into jobs, starts subagents (in parallel or in the background), answers their messages, and merges their reports. |

Switch modes in any of these ways:

- TUI: `/mode normal`, `/mode orchestrator`, or `/mode toggle`. Shift+Tab toggles too. The footer shows the current mode.
- Web UI: pick the mode in the agent picker, or run `/mode toggle`.

Saved sessions from older builds map onto the two modes. `autonomous` and
`monitor` become `orchestrator`. `plan`, `build`, and empty become `normal`.

A subagent session has a fixed role and cannot change mode.

## Subagents

In orchestrator mode the agent uses the `task` tool. Each subagent is a child
session with its own history. It cannot see the lead's conversation, so the
prompt must contain every detail it needs.

| Action | What it does |
|--------|--------------|
| `run` (default) | Starts a subagent and returns its final report. Several `run` calls in one message run in parallel. |
| `run` with `background: true` | Returns a `task_id` at once. The report arrives later as a message. |
| `run` with `task_id` and `prompt` | Resumes a finished subagent with its history. A running one cannot be resumed. |
| `status` | Lists the team, or one task. |
| `wait` | Blocks until a background task finishes or a message arrives (up to 3600 s). |
| `kill` | Stops a background task. Blocking subagents are stopped by the lead's interrupt instead. |
| `message` | Sends text to a running subagent, or to `lead`. |

### Retries

A subagent that fails with a transient provider error (rate limit, overload,
5xx, or timeout) is resumed in the same session up to two times, after 2 s and
then 4 s. Other errors are reported at once.

### Kill and status

- A kill requested before the subagent returns marks the task **killed**. Its partial output is kept, and the report says so.
- If the subagent finished before the kill arrived, the task stays **done**.

Status names:

| Status | Meaning |
|--------|---------|
| running | Still working. |
| done | Finished with a report. |
| error (Failed in the web UI) | Ended with a provider or runner error. |
| interrupted | Ended with no final answer, for example after a crash. |
| killed | Stopped by `kill` before it returned. |
| saved (web UI) | A stored subagent session that is no longer live. |

The TUI Subagents tab shows the total and the count per status, for example
`SUBAGENTS (3) · 1 running · 1 failed · 1 done`. The web panel shows the same
counts as a summary line under the heading.

### Opening a subagent

- TUI: open the Subagents tab and click a task. Press Esc to return to the parent.
- Web UI: click a subagent in the panel. The banner's **Back to parent** button returns you.

### Stopping a turn

Esc aborts the running turn in the TUI. A second Esc forces the UI to stop. In
the web UI, Escape cancels the active generation. Background subagents keep
running when the lead's turn ends. Stop one with `kill`.

## Keyboard shortcuts (TUI)

| Key | Action |
|-----|--------|
| Enter | Send the prompt. |
| Alt+Enter | New line in the prompt. |
| Shift+Tab | Toggle normal and orchestrator mode. |
| Esc | Close the open popup, leave a subagent view, or abort the running turn. |
| F3 | Copy mode. Mouse tracking is off, so the terminal can select text. Press again to turn it back on. |
| Ctrl+P | Command palette. |

Slash commands include `/mode`, `/compact [N]` (keeps the last N messages, default 4), `/help`, and `/stop`.

## Database maintenance

Space freed by deleted data stays in the database file until it is rewritten.
To reclaim it, run:

```sh
qcode db compact
```

The command prints the size before and after, in bytes. It needs the write
lock. If another qcode process is writing to the database, it refuses with
`another qcode process is using ...`, exits with status 1, and leaves the file
unchanged. A qcode process that is open but idle does not block it. Close the
other process (or stop `qcode serve`) and run the command again.
