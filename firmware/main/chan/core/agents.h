// Coding-agent status: events posted by hook scripts (Claude Code, Codex, Gemini CLI, …)
// and the in-memory store behind the Agents screen and alerts. Pure C++17, host-tested.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chan {

constexpr size_t kAgentEventMaxBytes = 4096;
constexpr size_t kAgentMessageMaxChars = 120;

enum class AgentState { Working, NeedsAttention, Done };

struct AgentEvent {
    std::string agent;    // "claude-code", "codex", "gemini", or anything else
    std::string session;  // per-session id so parallel sessions are separate rows
    std::string project;  // short label, usually the working directory's name
    std::string state;    // "working" | "needs_attention" | "done" | "ended"
    std::string message;  // optional, truncated to kAgentMessageMaxChars
};

// Validates and parses the POST body. Rejects bodies over kAgentEventMaxBytes, invalid JSON,
// missing/empty agent or session, and unknown states.
bool parse_agent_event(std::string_view json, AgentEvent& out, std::string& error);

struct AgentRow {
    std::string agent, session, project, message;
    AgentState state = AgentState::Working;
    uint32_t since_ms = 0;  // when this state was entered
    bool acknowledged = false;
};

enum class Change { None, BecameNeedsAttention, Other };

class AgentStore {
public:
    AgentStore(int max_sessions, uint32_t stale_ms, uint32_t renudge_ms);

    // Applies an event; key is agent+session. "ended" removes the row. Beyond max_sessions
    // the least recently updated row is evicted.
    Change apply(const AgentEvent& e, uint32_t now_ms);
    // Rows that are not stale, needs-attention first, then most recently updated first.
    std::vector<AgentRow> rows(uint32_t now_ms) const;
    bool any_unacknowledged_attention() const;
    // True exactly once per unacknowledged alert, renudge_ms after it was raised.
    bool take_renudge(uint32_t now_ms);
    void acknowledge_all();
    // {"agents":[{agent,session,project,state,message,seconds_ago,acknowledged}…]}
    std::string to_json(uint32_t now_ms) const;

private:
    struct Entry {
        AgentRow row;
        uint32_t updated_ms = 0;
        bool renudged = false;
    };
    std::vector<Entry> _entries;
    int _max_sessions;
    uint32_t _stale_ms;
    uint32_t _renudge_ms;
};

const char* agent_state_name(AgentState s);

}  // namespace chan
