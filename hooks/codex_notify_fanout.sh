#!/bin/bash
# Codex allows a single `notify` program. This fans the notification out to Chan and, optionally,
# to the notify program you already had (e.g. the Codex Computer Use client).
#
# ~/.codex/config.toml:
#   notify = ["/ABS/PATH/chan/hooks/codex_notify_fanout.sh"]
# or, keeping a previous notify command (its program and arguments after --prev):
#   notify = ["/ABS/PATH/chan/hooks/codex_notify_fanout.sh", "--prev", "/path/to/program", "arg1"]
#
# Codex appends the JSON payload as the last argument; both receivers get it unchanged.

HERE="$(cd "$(dirname "$0")" && pwd)"
PAYLOAD="${@: -1}"

# Chan (never blocks, always exits 0). Skipped when ~/.codex/hooks.json already reports to Chan:
# the hooks carry PermissionRequest and use a different session id, so this would be a duplicate row.
# Notify wrappers may run us without $HOME, so fall back to the account's home directory.
USER_HOME="${HOME:-$(eval echo "~$(id -un)")}"
if ! grep -q "chan_notify" "${CODEX_HOME:-$USER_HOME/.codex}/hooks.json" 2>/dev/null; then
    python3 "$HERE/chan_notify.py" --from codex "$PAYLOAD" >/dev/null 2>&1 &
fi

# Previous notify program: everything between --prev and the payload
if [ "$1" = "--prev" ] && [ $# -ge 3 ]; then
    shift
    PREV=("${@:1:$#-1}")
    "${PREV[@]}" "$PAYLOAD" >/dev/null 2>&1 &
fi
wait
exit 0
