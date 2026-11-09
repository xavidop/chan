#include "agents.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace chan {

namespace {
bool valid_state(const std::string& s)
{
    return s == "working" || s == "needs_attention" || s == "done" || s == "ended";
}

std::string str(JsonVariantConst v)
{
    return v.is<const char*>() ? std::string(v.as<const char*>()) : std::string();
}
}  // namespace

const char* agent_state_name(AgentState s)
{
    switch (s) {
        case AgentState::Working: return "working";
        case AgentState::NeedsAttention: return "needs_attention";
        case AgentState::Done: return "done";
    }
    return "working";
}

bool parse_agent_event(std::string_view json, AgentEvent& out, std::string& error)
{
    if (json.size() > kAgentEventMaxBytes) {
        error = "body too large";
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
        error = "invalid JSON";
        return false;
    }
    AgentEvent e;
    e.agent = str(doc["agent"]);
    e.session = str(doc["session"]);
    e.project = str(doc["project"]);
    e.state = str(doc["state"]);
    e.message = str(doc["message"]);
    if (e.agent.empty()) {
        error = "missing agent";
        return false;
    }
    if (e.session.empty()) {
        error = "missing session";
        return false;
    }
    if (!valid_state(e.state)) {
        error = "unknown state (use working, needs_attention, done or ended)";
        return false;
    }
    if (e.message.size() > kAgentMessageMaxChars) e.message.resize(kAgentMessageMaxChars);
    out = std::move(e);
    error.clear();
    return true;
}

AgentStore::AgentStore(int max_sessions, uint32_t stale_ms, uint32_t renudge_ms)
    : _max_sessions(max_sessions < 1 ? 1 : max_sessions), _stale_ms(stale_ms), _renudge_ms(renudge_ms)
{
}

Change AgentStore::apply(const AgentEvent& e, uint32_t now_ms)
{
    auto it = std::find_if(_entries.begin(), _entries.end(),
                           [&](const Entry& x) { return x.row.agent == e.agent && x.row.session == e.session; });

    if (e.state == "ended") {
        if (it != _entries.end()) {
            _entries.erase(it);
            return Change::Other;
        }
        return Change::None;
    }

    const AgentState new_state = e.state == "needs_attention" ? AgentState::NeedsAttention
                                 : e.state == "done"          ? AgentState::Done
                                                              : AgentState::Working;

    if (it == _entries.end()) {
        if (static_cast<int>(_entries.size()) >= _max_sessions) {
            auto oldest = std::min_element(_entries.begin(), _entries.end(),
                                           [](const Entry& a, const Entry& b) { return a.updated_ms < b.updated_ms; });
            _entries.erase(oldest);
        }
        Entry n;
        n.row.agent = e.agent;
        n.row.session = e.session;
        n.row.project = e.project;
        n.row.message = e.message;
        n.row.state = new_state;
        n.row.since_ms = now_ms;
        n.updated_ms = now_ms;
        _entries.push_back(std::move(n));
        return change_for(new_state);
    }

    Entry& x = *it;
    x.updated_ms = now_ms;
    if (!e.project.empty()) x.row.project = e.project;
    if (!e.message.empty() || new_state != x.row.state) x.row.message = e.message;
    if (new_state == x.row.state) return Change::None;

    x.row.state = new_state;
    x.row.since_ms = now_ms;
    x.row.acknowledged = false;
    x.renudged = false;
    return change_for(new_state);
}

Change AgentStore::change_for(AgentState entered)
{
    switch (entered) {
        case AgentState::NeedsAttention: return Change::BecameNeedsAttention;
        case AgentState::Done: return Change::BecameDone;
        default: return Change::Other;
    }
}

std::vector<AgentRow> AgentStore::rows(uint32_t now_ms) const
{
    std::vector<const Entry*> live;
    for (const auto& e : _entries) {
        if (now_ms - e.updated_ms < _stale_ms) live.push_back(&e);
    }
    std::sort(live.begin(), live.end(), [](const Entry* a, const Entry* b) {
        const bool aa = a->row.state == AgentState::NeedsAttention;
        const bool bb = b->row.state == AgentState::NeedsAttention;
        if (aa != bb) return aa;
        return a->updated_ms > b->updated_ms;
    });
    std::vector<AgentRow> out;
    out.reserve(live.size());
    for (const Entry* e : live) out.push_back(e->row);
    return out;
}

bool AgentStore::any_unacknowledged_attention() const
{
    for (const auto& e : _entries) {
        if (e.row.state == AgentState::NeedsAttention && !e.row.acknowledged) return true;
    }
    return false;
}

bool AgentStore::take_renudge(uint32_t now_ms)
{
    for (auto& e : _entries) {
        if (e.row.state == AgentState::NeedsAttention && !e.row.acknowledged && !e.renudged &&
            now_ms - e.row.since_ms >= _renudge_ms) {
            e.renudged = true;
            return true;
        }
    }
    return false;
}

void AgentStore::acknowledge_all()
{
    for (auto& e : _entries) {
        if (e.row.state == AgentState::NeedsAttention) {
            e.row.acknowledged = true;
            e.renudged = true;
        }
    }
}

std::string AgentStore::to_json(uint32_t now_ms) const
{
    JsonDocument doc;
    JsonArray arr = doc["agents"].to<JsonArray>();
    for (const auto& r : rows(now_ms)) {
        JsonObject o = arr.add<JsonObject>();
        o["agent"] = r.agent;
        o["session"] = r.session;
        o["project"] = r.project;
        o["state"] = agent_state_name(r.state);
        o["message"] = r.message;
        o["seconds_ago"] = static_cast<uint32_t>((now_ms - r.since_ms) / 1000);
        o["acknowledged"] = r.acknowledged;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace chan
