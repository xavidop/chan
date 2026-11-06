"""Tests for hooks/chan_notify.py (run with: python3 -m pytest hooks)."""

import http.server
import json
import pathlib
import socket
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import chan_notify as cn  # noqa: E402


def test_claude_notification_maps_to_needs_attention():
    ev = cn.build_event("claude", {"session_id": "s1", "cwd": "/Users/x/code/chan", "hook_event_name": "Notification",
                                   "message": "Claude needs your permission to use Bash"})
    assert ev == {"agent": "claude-code", "session": "s1", "project": "chan", "state": "needs_attention",
                  "message": "Claude needs your permission to use Bash"}


def test_claude_stop_maps_to_done():
    ev = cn.build_event("claude", {"session_id": "s1", "cwd": "/tmp/proj", "hook_event_name": "Stop"})
    assert ev["state"] == "done"
    assert ev["agent"] == "claude-code"


def test_claude_prompt_and_session_end():
    assert cn.build_event("claude", {"session_id": "s", "cwd": "/p", "hook_event_name": "UserPromptSubmit"})["state"] == "working"
    assert cn.build_event("claude", {"session_id": "s", "cwd": "/p", "hook_event_name": "SessionEnd"})["state"] == "ended"
    assert cn.build_event("claude", {"session_id": "s", "cwd": "/p", "hook_event_name": "PreToolUse"}) is None


def test_codex_turn_complete_maps_to_done():
    ev = cn.build_event("codex", {"type": "agent-turn-complete", "thread-id": "t9", "cwd": "/w/dash",
                                  "last-assistant-message": "All tests pass."})
    assert ev["agent"] == "codex"
    assert ev["session"] == "t9"
    assert ev["project"] == "dash"
    assert ev["state"] == "done"
    assert ev["message"] == "All tests pass."


def test_gemini_events():
    assert cn.build_event("gemini", {"session_id": "g", "cwd": "/p/x", "hook_event_name": "Notification"})["state"] == "needs_attention"
    assert cn.build_event("gemini", {"session_id": "g", "cwd": "/p/x", "hook_event_name": "AfterAgent"})["state"] == "done"
    assert cn.build_event("gemini", {"session_id": "g", "cwd": "/p/x", "hook_event_name": "AfterAgent"})["agent"] == "gemini"


def test_copilot_events_use_event_flag_and_camel_case_payload():
    ev = cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w/chan", "timestamp": 1}, event="notification")
    assert ev == {"agent": "copilot", "session": "cp1", "project": "chan", "state": "needs_attention", "message": ""}
    assert cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w"}, event="agentStop")["state"] == "done"
    assert cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w"}, event="sessionEnd")["state"] == "ended"
    assert cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w", "prompt": "fix tests"}, event="userPromptSubmitted")["message"] == "fix tests"
    assert cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w"}, event="preToolUse") is None
    assert cn.build_event("copilot", {"sessionId": "cp1", "cwd": "/w"}) is None


def test_token_falls_back_to_repo_env(monkeypatch, tmp_path):
    monkeypatch.delenv("CHAN_AGENT_TOKEN", raising=False)
    monkeypatch.delenv("CHAN_HOST", raising=False)
    monkeypatch.setattr(cn, "_repo_env", lambda: {"CHAN_AGENT_TOKEN": "from-file", "CHAN_HOST": "127.0.0.1:9"})
    seen = {}
    monkeypatch.setattr(cn, "post_event", lambda host, token, event, timeout: seen.update(host=host, token=token) or True)
    assert cn.main(["--from", "claude"], stdin_text=json.dumps({"session_id": "s", "cwd": "/p", "hook_event_name": "Stop"})) == 0
    assert seen == {"host": "127.0.0.1:9", "token": "from-file"}


def test_generic_requires_state():
    ev = cn.build_event("generic", {"agent": "cursor", "session": "c1", "state": "needs_attention", "cwd": "/a/b"})
    assert ev["agent"] == "cursor" and ev["project"] == "b"
    assert cn.build_event("generic", {"agent": "cursor", "session": "c1"}) is None


def test_project_is_cwd_basename():
    assert cn.project_from_cwd("/Users/x/Documents/personal/repos/chan") == "chan"
    assert cn.project_from_cwd("/Users/x/Documents/personal/repos/chan/") == "chan"
    assert cn.project_from_cwd("") == ""


def test_message_truncated():
    ev = cn.build_event("claude", {"session_id": "s", "cwd": "/p", "hook_event_name": "Notification", "message": "x" * 500})
    assert len(ev["message"]) == 120


def test_notify_exits_0_when_host_unreachable(monkeypatch):
    monkeypatch.setenv("CHAN_HOST", "127.0.0.1:9")  # discard port: connection refused immediately
    monkeypatch.setenv("CHAN_AGENT_TOKEN", "t")
    t0 = time.time()
    rc = cn.main(["--from", "claude"], stdin_text=json.dumps({"session_id": "s", "cwd": "/p", "hook_event_name": "Stop"}))
    assert rc == 0
    assert time.time() - t0 < 1.5


def test_notify_exits_0_on_timeout(monkeypatch):
    # a listening socket that never answers
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    monkeypatch.setenv("CHAN_HOST", f"127.0.0.1:{port}")
    monkeypatch.setenv("CHAN_AGENT_TOKEN", "t")
    t0 = time.time()
    rc = cn.main(["--from", "claude", "--timeout", "0.5"], stdin_text=json.dumps({"session_id": "s", "cwd": "/p", "hook_event_name": "Stop"}))
    srv.close()
    assert rc == 0
    assert time.time() - t0 < 1.5


class _Server(http.server.HTTPServer):
    def server_bind(self):  # skip getfqdn(): reverse DNS on 127.0.0.1 can stall for ~30 s on macOS
        socket.socket.bind(self.socket, self.server_address)
        self.server_address = self.socket.getsockname()
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


class _Handler(http.server.BaseHTTPRequestHandler):
    received = []

    def do_POST(self):  # noqa: N802
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        _Handler.received.append((self.path, self.headers.get("Authorization"), json.loads(body)))
        self.send_response(204)
        self.end_headers()

    def log_message(self, *args):  # silence
        pass


def test_token_sent_as_bearer(monkeypatch):
    server = _Server(("127.0.0.1", 0), _Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.handle_request, daemon=True)
    thread.start()
    monkeypatch.setenv("CHAN_HOST", f"127.0.0.1:{port}")
    monkeypatch.setenv("CHAN_AGENT_TOKEN", "secret-token")
    rc = cn.main(["--from", "codex", json.dumps({"type": "agent-turn-complete", "thread-id": "t1", "cwd": "/w/chan"})])
    thread.join(timeout=3)
    server.server_close()
    assert rc == 0
    assert len(_Handler.received) == 1
    path, auth, body = _Handler.received[0]
    assert path == "/api/agents"
    assert auth == "Bearer secret-token"
    assert body["agent"] == "codex" and body["state"] == "done" and body["project"] == "chan"
