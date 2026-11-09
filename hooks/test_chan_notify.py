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


def test_codex_hook_events_from_hooks_json():
    perm = cn.build_event("codex", {"hook_event_name": "PermissionRequest", "session_id": "thr_1", "cwd": "/w/dash",
                                    "tool_name": "Bash", "tool_input": {"command": "rm -rf build", "description": None}})
    assert perm == {"agent": "codex", "session": "thr_1", "project": "dash", "state": "needs_attention",
                    "message": "Bash: rm -rf build"}
    assert cn.build_event("codex", {"hook_event_name": "UserPromptSubmit", "session_id": "thr_1", "cwd": "/w",
                                    "prompt": "fix the tests"}) == {"agent": "codex", "session": "thr_1", "project": "w",
                                                                    "state": "working", "message": "fix the tests"}
    stop = cn.build_event("codex", {"hook_event_name": "Stop", "session_id": "thr_1", "cwd": "/w", "last_assistant_message": "Done."})
    assert stop["state"] == "done" and stop["message"] == "Done."
    assert cn.build_event("codex", {"hook_event_name": "SessionEnd", "session_id": "thr_1", "cwd": "/w"})["state"] == "ended"
    assert cn.build_event("codex", {"hook_event_name": "PreToolUse", "session_id": "thr_1", "cwd": "/w"}) is None


def test_antigravity_events():
    common = {"conversationId": "ec33", "workspacePaths": ["/workspace/project"], "lastUserInput": "add tests"}
    first = cn.build_event("antigravity", {**common, "invocationNum": 0, "initialNumSteps": 0}, event="PreInvocation")
    assert first == {"agent": "antigravity", "session": "ec33", "project": "project", "state": "working", "message": "add tests"}
    # later model calls of the same run are not re-reported (PreInvocation fires before every model call)
    assert cn.build_event("antigravity", {**common, "invocationNum": 3}, event="PreInvocation") is None
    assert cn.build_event("antigravity", common, event="SessionStart")["state"] == "working"
    stop = cn.build_event("antigravity", {**common, "terminationReason": "model_stop", "fullyIdle": True}, event="Stop")
    assert stop["state"] == "done" and stop["message"] == ""
    assert cn.build_event("antigravity", common, event="PostToolUse") is None
    assert cn.build_event("antigravity", {}, event="Stop")["session"] == "default"


def test_antigravity_stop_answers_with_a_decision(monkeypatch, capsys):
    """Antigravity requires a JSON decision on stdout for Stop; other events get {}."""
    monkeypatch.setenv("CHAN_HOST", "127.0.0.1:9")
    monkeypatch.setenv("CHAN_AGENT_TOKEN", "t")
    assert cn.main(["--from", "antigravity", "--event", "Stop"], stdin_text=json.dumps({"conversationId": "c"})) == 0
    assert json.loads(capsys.readouterr().out) == {"decision": "stop"}
    assert cn.main(["--from", "antigravity", "--event", "PreInvocation"], stdin_text=json.dumps({"conversationId": "c", "invocationNum": 0})) == 0
    assert json.loads(capsys.readouterr().out) == {}
    # other sources keep stdout empty (Codex' Stop treats plain text as invalid)
    assert cn.main(["--from", "codex"], stdin_text=json.dumps({"hook_event_name": "Stop", "session_id": "s", "cwd": "/w"})) == 0
    assert capsys.readouterr().out == ""


def test_ip_literal_host_skips_resolution(tmp_path):
    def resolver(name):
        raise AssertionError("must not resolve an IP literal")
    assert cn.resolve_host("192.168.0.37", tmp_path, resolver) == "192.168.0.37"
    assert cn.resolve_host("192.168.0.37:8080", tmp_path, resolver) == "192.168.0.37:8080"


def test_resolved_ip_is_cached_and_reused(tmp_path):
    calls = []

    def resolver(name):
        calls.append(name)
        return "192.0.2.5"

    assert cn.resolve_host("chan.local", tmp_path, resolver) == "192.0.2.5"
    assert cn.resolve_host("chan.local:8080", tmp_path, resolver) == "192.0.2.5:8080"
    assert calls == ["chan.local"]  # second call served from the cache
    assert (tmp_path / "chan.local").read_text().strip() == "192.0.2.5"


def test_deliver_falls_back_to_a_fresh_lookup_when_the_cached_ip_fails(monkeypatch, tmp_path):
    (tmp_path / "chan.local").write_text("198.51.100.1\n")
    posted = []

    def fake_post(host, token, event, timeout):
        posted.append(host)
        return host == "198.51.100.2"

    monkeypatch.setattr(cn, "post_event", fake_post)
    ok = cn.deliver("chan.local", "t", {"agent": "x"}, 2.0, tmp_path, resolver=lambda name: "198.51.100.2")
    assert ok is True
    assert posted == ["198.51.100.1", "198.51.100.2"]
    assert (tmp_path / "chan.local").read_text().strip() == "198.51.100.2"


def test_deliver_gives_up_quietly_when_the_name_does_not_resolve(monkeypatch, tmp_path):
    monkeypatch.setattr(cn, "post_event", lambda *a: (_ for _ in ()).throw(AssertionError("no address to post to")))
    assert cn.deliver("chan.local", "t", {"agent": "x"}, 2.0, tmp_path, resolver=lambda name: None) is False
    assert not (tmp_path / "chan.local").exists()


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


def _serve_one(monkeypatch):
    server = _Server(("127.0.0.1", 0), _Handler)
    thread = threading.Thread(target=server.handle_request, daemon=True)
    thread.start()
    monkeypatch.setenv("CHAN_HOST", f"127.0.0.1:{server.server_address[1]}")
    monkeypatch.setenv("CHAN_AGENT_TOKEN", "t")
    return server, thread


def test_codex_fanout_skips_chan_when_hooks_json_already_reports(monkeypatch, tmp_path):
    """With ~/.codex/hooks.json installed, the legacy notify fan-out must not post a duplicate row."""
    import subprocess
    script = pathlib.Path(__file__).parent / "codex_notify_fanout.sh"
    payload = json.dumps({"type": "agent-turn-complete", "thread-id": "t2", "cwd": "/w/chan"})
    _Handler.received.clear()

    codex_home = tmp_path / "codex"
    codex_home.mkdir()
    (codex_home / "hooks.json").write_text(json.dumps({"hooks": {"Stop": [{"hooks": [{"command": "python3 x/chan_notify.py --from codex"}]}]}}))
    server, thread = _serve_one(monkeypatch)
    monkeypatch.setenv("CODEX_HOME", str(codex_home))
    # notify wrappers (e.g. Codex Computer Use) run the previous notify program without $HOME
    env = {k: v for k, v in __import__("os").environ.items() if k != "HOME"}
    subprocess.run(["bash", str(script), payload], env=env, timeout=10, check=True)
    thread.join(timeout=1.0)
    server.server_close()
    assert _Handler.received == []

    # without hooks.json the fan-out still reports
    monkeypatch.setenv("CODEX_HOME", str(tmp_path / "empty"))
    server, thread = _serve_one(monkeypatch)
    env = {k: v for k, v in __import__("os").environ.items()}
    subprocess.run(["bash", str(script), payload], env=env, timeout=10, check=True)
    thread.join(timeout=3)
    server.server_close()
    assert len(_Handler.received) == 1 and _Handler.received[0][2]["agent"] == "codex"
    _Handler.received.clear()


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
