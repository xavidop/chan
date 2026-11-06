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

# Chan (never blocks: 2 s timeout, always exits 0)
python3 "$HERE/chan_notify.py" --from codex "$PAYLOAD" >/dev/null 2>&1 &

# Previous notify program: everything between --prev and the payload
if [ "$1" = "--prev" ] && [ $# -ge 3 ]; then
    shift
    PREV=("${@:1:$#-1}")
    "${PREV[@]}" "$PAYLOAD" >/dev/null 2>&1 &
fi
wait
exit 0
