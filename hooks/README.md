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

`python3 hooks/install_hooks.py` writes the Claude Code, Codex, Copilot CLI and Antigravity
configuration below for you (merging into existing files, with a `.bak` copy); `--dry-run` shows the changes.

**Slow `chan.local`:** on a Mac with a VPN client, resolving `chan.local` can take a flat 5 s
(the first resolver times out before mDNS answers) — longer than some agents give a hook. The
script therefore remembers the robot's IP in `~/.cache/chan/chan.local` after the first lookup
and tries it first; only a failed POST triggers a fresh lookup. Hook timeouts below are 15 s so
that first lookup survives. Delete the cache file if you move the robot to another network.

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

Codex has lifecycle hooks (`~/.codex/hooks.json`, enabled by default since 0.16x). Add:

```json
{
  "hooks": {
    "UserPromptSubmit":  [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from codex", "timeout": 15 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from codex", "timeout": 15 }] }],
    "Stop":              [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from codex", "timeout": 15 }] }],
    "SessionEnd":        [{ "hooks": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from codex", "timeout": 3 }] }]
  }
}
```

| Codex event | Chan state |
|---|---|
| `PermissionRequest` (about to ask for approval) | **needs attention** (message: tool + command) |
| `UserPromptSubmit` | working |
| `Stop` | done |
| `SessionEnd` | row removed |

**Codex only runs hooks you have reviewed:** open Codex, type `/hooks`, and trust the four Chan
entries once (again whenever the command line changes). The hook prints nothing on stdout, so
the normal approval prompt still appears.

Older `notify` program: `agent-turn-complete` → **done** only (Codex never sends approvals this
way). It is still supported — Codex accepts one `notify` program, so keep an existing one (for
example the Codex Computer Use client) with the fan-out wrapper:

```toml
notify = ["/ABS/PATH/chan/hooks/codex_notify_fanout.sh", "--prev", "/path/to/previous-program", "its-arg"]
```

When `hooks.json` reports to Chan, the fan-out skips Chan so a turn is not reported twice.

## 3b. GitHub Copilot CLI

Copilot CLI reads personal hooks from `~/.copilot/hooks/*.json`
([reference](https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-hooks-reference)).
Create `~/.copilot/hooks/chan.json`:

```json
{
  "version": 1,
  "hooks": {
    "sessionStart":        [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event sessionStart", "timeoutSec": 15 }],
    "userPromptSubmitted": [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event userPromptSubmitted", "timeoutSec": 15 }],
    "notification":        [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event notification", "timeoutSec": 15 }],
    "agentStop":           [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event agentStop", "timeoutSec": 15 }],
    "sessionEnd":          [{ "type": "command", "bash": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from copilot --event sessionEnd", "timeoutSec": 15 }]
  }
}
```

Copilot's camelCase payloads carry `sessionId` and `cwd` but not the event name, hence `--event`.
`notification` (permission prompts, elicitation dialogs, idle) → **needs attention**, `agentStop` → done.
Hook files are loaded when the CLI starts. Keep `timeoutSec` well above 5: Copilot kills the hook
at the deadline, and the first `chan.local` lookup alone can take 5 s (see "Slow chan.local").

## 3c. Google Antigravity (IDE, app and `agy` CLI)

Antigravity reads `~/.gemini/config/hooks.json` (global) or `<workspace>/.agents/hooks.json`;
hooks are grouped under a name of your choice. Timeouts are in seconds.

```json
{
  "chan": {
    "SessionStart":  [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from antigravity --event SessionStart", "timeout": 15 }],
    "PreInvocation": [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from antigravity --event PreInvocation", "timeout": 15 }],
    "Stop":          [{ "type": "command", "command": "python3 /ABS/PATH/chan/hooks/chan_notify.py --from antigravity --event Stop", "timeout": 15 }]
  }
}
```

| Antigravity event | Chan state |
|---|---|
| `SessionStart`, `PreInvocation` with `invocationNum` 0 (first model call of a run) | working |
| `Stop` | done (the hook answers `{"decision":"stop"}` as Antigravity requires) |

Antigravity has **no event for "waiting for your approval"** (only SessionStart, Pre/PostToolUse,
Pre/PostInvocation, Stop), so an Antigravity row can be *working* or *done* but never *needs
attention*; use Antigravity's own notification setting for approvals. Each conversation is a row
(`conversationId`), labelled with the first workspace folder. You can toggle the group from
Settings → Customizations → Hooks.

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
