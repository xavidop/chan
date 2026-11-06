# Telling Chan about your coding agents

Chan shows your coding agents on its **Agents** screen and gets your attention (chime,
orange breathing LEDs, worried face, head turned to you) when one of them needs you.
Agents report through tiny hook scripts that POST to the robot on your Wi-Fi.

Everything goes through one script: [`chan_notify.py`](chan_notify.py) (Python 3,
standard library only, never slows the agent down: 2 s timeout, always exits 0).

## 1. Shared settings

The script reads `CHAN_AGENT_TOKEN` (and optionally `CHAN_HOST`) from the environment, and
falls back to the repo's own `.env` next to it — so if you run the hooks from this checkout,
nothing else is needed. To run them from elsewhere, export them in your shell profile:

```bash
export CHAN_AGENT_TOKEN="…same value as CHAN_AGENT_TOKEN in .env…"
export CHAN_HOST="chan.local"   # or the robot's IP; default is chan.local
```

`python3 hooks/install_hooks.py` writes the Claude Code, Codex and Copilot CLI configuration
below for you (merging into existing files, with a `.bak` copy); `--dry-run` shows the changes.

Check the robot is reachable:

```bash
curl http://chan.local/api/health
```

If `chan.local` does not resolve (some VPN clients, e.g. NordVPN, hijack `.local` lookups and
block LAN traffic until you enable "local network discovery"), use the robot's IP instead:
`export CHAN_HOST=192.168.x.y` — the IP is printed in the boot log (`Got IP:`) and shown in
Settings → Wi-Fi → Status.

## 2. Claude Code

Add to `~/.claude/settings.json` (or a project's `.claude/settings.json`). Replace the path.

```json
{
  "hooks": {
    "Notification":     [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from claude" }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from claude" }] }],
    "Stop":             [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from claude" }] }],
    "SessionEnd":       [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from claude" }] }]
  }
}
```

| Claude Code event | Chan state |
|---|---|
| `Notification` (permission prompt, waiting for input) | **needs attention** |
| `UserPromptSubmit` | working |
| `Stop` | done |
| `SessionEnd` | row removed |

Each Claude Code session is its own row (`session_id`), labelled with the project folder name (`cwd`).

## 3. Codex

In `~/.codex/config.toml`:

```toml
notify = ["python3", "/ABS/PATH/chan/hooks/chan_notify.py", "--from", "codex"]
```

Codex passes a JSON payload as the last argument; `agent-turn-complete` → **done**.
Codex does not (yet) emit an approval event through `notify`; when it does, `approval-requested`
is already mapped to **needs attention**.

Codex accepts only one `notify` program. If you already have one (for example the Codex
Computer Use client), keep it with the fan-out wrapper — its arguments after `--prev` are the
previous program and its arguments:

```toml
notify = ["/ABS/PATH/chan/hooks/codex_notify_fanout.sh", "--prev", "/path/to/previous-program", "its-arg"]
```

## 3b. GitHub Copilot CLI

Copilot CLI reads personal hooks from `~/.copilot/hooks/*.json`
([reference](https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-hooks-reference)).
Create `~/.copilot/hooks/chan.json`:

```json
{
  "version": 1,
  "hooks": {
    "sessionStart":        [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event sessionStart", "timeoutSec": 5 }],
    "userPromptSubmitted": [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event userPromptSubmitted", "timeoutSec": 5 }],
    "notification":        [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event notification", "timeoutSec": 5 }],
    "agentStop":           [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event agentStop", "timeoutSec": 5 }],
    "sessionEnd":          [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event sessionEnd", "timeoutSec": 5 }]
  }
}
```

Copilot's camelCase payloads carry `sessionId` and `cwd` but not the event name, hence `--event`.
`notification` (permission prompts, elicitation dialogs, idle) → **needs attention**, `agentStop` → done.
Hook files are loaded when the CLI starts.

## 4. Gemini CLI

In `~/.gemini/settings.json`:

```json
{
  "hooks": {
    "Notification": [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from gemini" }] }],
    "BeforeAgent":  [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from gemini" }] }],
    "AfterAgent":   [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from gemini" }] }],
    "SessionEnd":   [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from gemini" }] }]
  }
}
```

Event names follow Gemini CLI's hook documentation; if your version names them differently,
adjust the keys — the script only looks at `hook_event_name`, `session_id`, `cwd` and `message`.

## 5. Anything else

```bash
python3 /ABS/PATH/chan/hooks/chan_notify.py --from generic --agent cursor --state needs_attention --session abc
```

or `curl` directly:

```bash
curl -H "Authorization: Bearer $CHAN_AGENT_TOKEN" \
     -d '{"agent":"cursor","session":"abc","project":"myrepo","state":"needs_attention","message":"Approve edit?"}' \
     http://chan.local/api/agents
```

## API

| Method | Path | Auth | Body / response |
|---|---|---|---|
| GET | `/api/health` | none | `{"ok":true,"name":"Chan"}` |
| POST | `/api/agents` | `Authorization: Bearer <token>` | `{"agent","session","project","state","message"}` → `204`; `400` invalid, `401` bad token, `413` over 4096 bytes |
| GET | `/api/agents` | bearer | current rows as JSON |

States: `working`, `needs_attention`, `done`, `ended` (removes the row). Rows older than
`agents.staleMinutes` disappear; at most `agents.maxSessions` rows are kept; an unacknowledged
alert re-nudges once after `agents.renudgeMinutes`. A head pat, or talking to Chan, acknowledges.
