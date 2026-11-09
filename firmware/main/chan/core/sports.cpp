#include "sports.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace chan {

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string str(JsonVariantConst v)
{
    if (v.is<const char*>()) return v.as<const char*>();
    if (v.is<long>()) return std::to_string(v.as<long>());
    return "";
}

int score_of(JsonVariantConst v)
{
    if (v.is<int>()) return v.as<int>();
    if (v.is<double>()) return static_cast<int>(v.as<double>());
    if (v.is<const char*>()) return std::atoi(v.as<const char*>());
    // ESPN team schedules: {"value": 3.0, "displayValue": "3"}
    if (v["displayValue"].is<const char*>()) return std::atoi(v["displayValue"].as<const char*>());
    if (v["value"].is<double>()) return static_cast<int>(v["value"].as<double>());
    return 0;
}

const char* sport_path(Sport s)
{
    switch (s) {
        case Sport::Football: return "football";
        case Sport::Basketball: return "basketball";
        case Sport::Hockey: return "hockey";
        case Sport::Baseball: return "baseball";
        case Sport::Soccer: return "soccer";
    }
    return "soccer";
}

int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

Sport sport_for_league(std::string_view league)
{
    if (league == "nfl") return Sport::Football;
    if (league == "nba") return Sport::Basketball;
    if (league == "nhl") return Sport::Hockey;
    if (league == "mlb") return Sport::Baseball;
    return Sport::Soccer;
}

const char* game_status_name(GameStatus s)
{
    switch (s) {
        case GameStatus::Scheduled: return "scheduled";
        case GameStatus::Live: return "live";
        case GameStatus::Final: return "final";
    }
    return "scheduled";
}

std::string espn_scoreboard_url(const std::string& league, const std::string& yyyymmdd)
{
    std::string url = std::string("http://site.api.espn.com/apis/site/v2/sports/") + sport_path(sport_for_league(league)) + "/" +
                      league + "/scoreboard";
    if (!yyyymmdd.empty()) url += "?dates=" + yyyymmdd;
    return url;
}

std::string mlb_schedule_url(const std::string& yyyy_mm_dd)
{
    return "http://statsapi.mlb.com/api/v1/schedule?sportId=1&date=" + yyyy_mm_dd + "&hydrate=linescore";
}

const JsonDocument& espn_filter()
{
    static JsonDocument f;
    if (f.isNull()) {
        deserializeJson(f, R"({"events":[{"id":true,"date":true,"shortName":true,
          "status":{"type":{"state":true,"completed":true,"shortDetail":true},"displayClock":true,"period":true},
          "competitions":[{"status":{"type":{"state":true,"completed":true,"shortDetail":true}},
            "competitors":[{"homeAway":true,"score":true,
            "team":{"id":true,"displayName":true,"shortDisplayName":true,"abbreviation":true,"name":true}}]}]}]})");
    }
    return f;
}

std::string espn_teams_url(const std::string& league)
{
    return std::string("http://site.api.espn.com/apis/site/v2/sports/") + sport_path(sport_for_league(league)) + "/" + league + "/teams";
}

std::string espn_team_schedule_url(const std::string& league, const std::string& team_id)
{
    return std::string("http://site.api.espn.com/apis/site/v2/sports/") + sport_path(sport_for_league(league)) + "/" + league +
           "/teams/" + team_id + "/schedule";
}

std::string mlb_teams_url()
{
    return "http://statsapi.mlb.com/api/v1/teams?sportId=1";
}

std::string mlb_team_schedule_url(const std::string& team_id, int season)
{
    return "http://statsapi.mlb.com/api/v1/schedule?sportId=1&teamId=" + team_id + "&season=" + std::to_string(season) +
           "&gameType=R,F,D,L,W";
}

const JsonDocument& espn_teams_filter()
{
    static JsonDocument f;
    if (f.isNull()) {
        deserializeJson(f, R"({"sports":[{"leagues":[{"teams":[{"team":{"id":true,"displayName":true,"shortDisplayName":true,"abbreviation":true,"name":true,"nickname":true}}]}]}]})");
    }
    return f;
}

const JsonDocument& mlb_teams_filter()
{
    static JsonDocument f;
    if (f.isNull()) {
        deserializeJson(f, R"({"teams":[{"id":true,"name":true,"abbreviation":true,"teamName":true}]})");
    }
    return f;
}

std::vector<TeamRef> parse_espn_teams(JsonVariantConst root)
{
    std::vector<TeamRef> out;
    for (JsonObjectConst sport : root["sports"].as<JsonArrayConst>()) {
        for (JsonObjectConst league : sport["leagues"].as<JsonArrayConst>()) {
            for (JsonObjectConst t : league["teams"].as<JsonArrayConst>()) {
                JsonObjectConst team = t["team"].as<JsonObjectConst>();
                TeamRef r{str(team["id"]), str(team["displayName"]), str(team["abbreviation"])};
                if (r.name.empty()) r.name = str(team["name"]);
                if (!r.id.empty()) out.push_back(std::move(r));
            }
        }
    }
    return out;
}

std::vector<TeamRef> parse_mlb_teams(JsonVariantConst root)
{
    std::vector<TeamRef> out;
    for (JsonObjectConst t : root["teams"].as<JsonArrayConst>()) {
        TeamRef r{str(t["id"]), str(t["name"]), str(t["abbreviation"])};
        // keep the nickname ("Blue Jays") reachable through the abbrev slot's sibling: append to name match list
        if (!r.id.empty()) {
            out.push_back(r);
            const std::string nick = str(t["teamName"]);
            if (!nick.empty()) out.push_back(TeamRef{r.id, nick, r.abbrev});
        }
    }
    return out;
}

std::vector<TeamRef> parse_espn_teams(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size(), DeserializationOption::NestingLimit(32)) != DeserializationError::Ok) return {};
    return parse_espn_teams(doc.as<JsonVariantConst>());
}

std::vector<TeamRef> parse_mlb_teams(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size(), DeserializationOption::NestingLimit(32)) != DeserializationError::Ok) return {};
    return parse_mlb_teams(doc.as<JsonVariantConst>());
}

std::string find_team_id(const std::vector<TeamRef>& refs, const Team& team)
{
    const std::string tn = lower(team.name), td = lower(team.display_name);
    for (const auto& r : refs) {
        const std::string n = lower(r.name), a = lower(r.abbrev);
        if (n == tn || n == td || (!a.empty() && (a == tn || a == td))) return r.id;
    }
    for (const auto& r : refs) {
        const std::string n = lower(r.name);
        if (!tn.empty() && n.find(tn) != std::string::npos) return r.id;
    }
    return "";
}

const JsonDocument& mlb_filter()
{
    static JsonDocument f;
    if (f.isNull()) {
        deserializeJson(f, R"({"dates":[{"games":[{"gamePk":true,"gameDate":true,
          "status":{"abstractGameState":true,"detailedState":true},
          "teams":{"home":{"score":true,"team":{"id":true,"name":true}},"away":{"score":true,"team":{"id":true,"name":true}}},
          "linescore":{"currentInning":true,"inningState":true,"inningHalf":true}}]}]})");
    }
    return f;
}

uint64_t parse_iso8601_utc(std::string_view text)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    std::string t(text);
    int n = sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s);
    if (n < 5 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    if (n == 5) s = 0;
    const int64_t days = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    return static_cast<uint64_t>(days * 86400 + h * 3600 + mi * 60 + s);
}

std::vector<Game> parse_espn_scoreboard(std::string_view json, const std::string& league)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size(), DeserializationOption::NestingLimit(32)) != DeserializationError::Ok) return {};
    return parse_espn_scoreboard(doc.as<JsonVariantConst>(), league);
}

std::vector<Game> parse_mlb_schedule(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size(), DeserializationOption::NestingLimit(32)) != DeserializationError::Ok) return {};
    return parse_mlb_schedule(doc.as<JsonVariantConst>());
}

std::vector<Game> parse_espn_scoreboard(JsonVariantConst doc, const std::string& league)
{
    std::vector<Game> out;
    for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
        Game g;
        g.id = str(ev["id"]);
        g.league = league;
        g.start_epoch = parse_iso8601_utc(str(ev["date"]));
        JsonArrayConst comps_for_status = ev["competitions"].as<JsonArrayConst>();
        JsonObjectConst st = ev["status"].as<JsonObjectConst>();
        if (st.isNull() && !comps_for_status.isNull() && comps_for_status.size() > 0) {
            st = comps_for_status[0]["status"].as<JsonObjectConst>();  // team schedules only have it here
        }
        const std::string state = str(st["type"]["state"]);
        g.status = state == "in" ? GameStatus::Live : state == "post" ? GameStatus::Final : GameStatus::Scheduled;
        if (g.status == GameStatus::Live) {
            const std::string clock = str(st["displayClock"]);
            const int period = st["period"].as<int>();
            const std::string detail = str(st["type"]["shortDetail"]);
            if (!detail.empty()) g.clock = detail;
            else if (period > 0) g.clock = "P" + std::to_string(period) + " " + clock;
            else g.clock = clock;
        } else {
            g.clock = g.status == GameStatus::Final ? "Final" : str(st["type"]["shortDetail"]);
        }
        JsonArrayConst comps = ev["competitions"].as<JsonArrayConst>();
        if (comps.isNull() || comps.size() == 0) continue;
        for (JsonObjectConst c : comps[0]["competitors"].as<JsonArrayConst>()) {
            Side side;
            side.name = str(c["team"]["displayName"]);
            if (side.name.empty()) side.name = str(c["team"]["name"]);
            side.abbrev = str(c["team"]["abbreviation"]);
            side.score = score_of(c["score"]);
            if (str(c["homeAway"]) == "home") g.home = side;
            else g.away = side;
        }
        if (g.id.empty() || g.home.name.empty() || g.away.name.empty()) continue;
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<Game> parse_mlb_schedule(JsonVariantConst doc)
{
    std::vector<Game> out;
    for (JsonObjectConst date : doc["dates"].as<JsonArrayConst>()) {
        for (JsonObjectConst gm : date["games"].as<JsonArrayConst>()) {
            Game g;
            g.id = str(gm["gamePk"]);
            g.league = "mlb";
            g.start_epoch = parse_iso8601_utc(str(gm["gameDate"]));
            const std::string state = str(gm["status"]["abstractGameState"]);
            g.status = state == "Live" ? GameStatus::Live : state == "Final" ? GameStatus::Final : GameStatus::Scheduled;
            g.home.name = str(gm["teams"]["home"]["team"]["name"]);
            g.home.score = score_of(gm["teams"]["home"]["score"]);
            g.away.name = str(gm["teams"]["away"]["team"]["name"]);
            g.away.score = score_of(gm["teams"]["away"]["score"]);
            if (g.status == GameStatus::Live) {
                const std::string half = str(gm["linescore"]["inningState"]);
                const int inning = gm["linescore"]["currentInning"].as<int>();
                g.clock = half + (inning > 0 ? " " + std::to_string(inning) : "");
            } else {
                g.clock = g.status == GameStatus::Final ? "Final" : str(gm["status"]["detailedState"]);
            }
            if (g.id.empty() || g.home.name.empty() || g.away.name.empty()) continue;
            out.push_back(std::move(g));
        }
    }
    return out;
}

bool game_involves(const Game& g, const Team& t)
{
    if (g.league != t.league) return false;
    auto matches = [&](const Side& s) {
        const std::string n = lower(s.name), a = lower(s.abbrev);
        const std::string tn = lower(t.name), td = lower(t.display_name);
        if (n.empty()) return false;
        if (n == tn || n == td) return true;
        if (!a.empty() && (a == tn || a == td)) return true;
        return !tn.empty() && n.find(tn) != std::string::npos;
    };
    return matches(g.home) || matches(g.away);
}

const Team* followed_side(const Game& g, bool home, const std::vector<Team>& teams)
{
    Game only = g;
    if (home) only.away = Side{};
    else only.home = Side{};
    for (const auto& t : teams) {
        if (game_involves(only, t)) return &t;
    }
    return nullptr;
}

std::string score_line(Sport sport, int points)
{
    switch (sport) {
        case Sport::Soccer:
        case Sport::Hockey: return "GOAL!";
        case Sport::Baseball: return "RUN!";
        case Sport::Football:
            if (points >= 6) return "TOUCHDOWN!";
            if (points == 3) return "FIELD GOAL!";
            if (points == 2) return "SAFETY!";
            if (points == 1) return "EXTRA POINT!";
            return "SCORE!";
        case Sport::Basketball:
            if (points >= 3) return "THREE!";
            if (points == 1) return "FREE THROW!";
            return "BASKET!";
    }
    return "SCORE!";
}

std::vector<ScoreEvent> ScoreTracker::update(const std::vector<Game>& games, const std::vector<Team>& followed)
{
    std::set<std::string> leagues;
    for (const auto& g : games) leagues.insert(g.league);
    return update(games, followed, leagues);
}

std::vector<ScoreEvent> ScoreTracker::update(const std::vector<Game>& games, const std::vector<Team>& followed,
                                             const std::set<std::string>& fetched_leagues)
{
    _followed = followed;
    auto involves_followed = [&](const Game& g) {
        for (const auto& t : followed) {
            if (game_involves(g, t)) return true;
        }
        return false;
    };
    // Drop games of successfully fetched leagues that this snapshot no longer lists.
    std::set<std::string> listed;
    for (const auto& g : games) listed.insert(g.id);
    for (auto it = _games.begin(); it != _games.end();) {
        if (fetched_leagues.count(it->second.league) && !listed.count(it->first)) it = _games.erase(it);
        else ++it;
    }
    std::vector<ScoreEvent> events;
    for (const auto& g : games) {
        if (!involves_followed(g)) continue;
        auto it = _games.find(g.id);
        if (it != _games.end()) {
            const Game& prev = it->second;
            const Team* home_team = followed_side(g, true, followed);
            const Team* away_team = followed_side(g, false, followed);
            auto emit = [&](int delta, bool home_scored) {
                if (delta <= 0) return;
                const Team* scorer = home_scored ? home_team : away_team;
                const Team* other = home_scored ? away_team : home_team;
                if (!scorer && !other) return;
                ScoreEvent e;
                e.game_id = g.id;
                e.points = delta;
                e.sport = sport_for_league(g.league);
                e.line = score_line(e.sport, delta);
                if (scorer) {
                    e.ours = true;
                    e.team_key = scorer->key;
                } else {
                    e.ours = false;
                    e.team_key = other->key;
                }
                events.push_back(std::move(e));
            };
            emit(g.home.score - prev.home.score, true);
            emit(g.away.score - prev.away.score, false);
        }
        _games[g.id] = g;
    }
    remember_results(games, followed);
    return events;
}

void ScoreTracker::remember_results(const std::vector<Game>& games, const std::vector<Team>& followed)
{
    for (const auto& g : games) {
        if (g.status != GameStatus::Final) continue;
        for (const auto& t : followed) {
            if (!game_involves(g, t)) continue;
            auto it = _last_results.find(t.key);
            if (it == _last_results.end() || g.start_epoch >= it->second.start_epoch) _last_results[t.key] = g;
        }
    }
}

std::vector<Game> ScoreTracker::followed_games() const
{
    std::vector<Game> out;
    for (const auto& [id, g] : _games) {
        for (const auto& t : _followed) {
            if (game_involves(g, t)) {
                out.push_back(g);
                break;
            }
        }
    }
    auto rank = [](GameStatus s) { return s == GameStatus::Live ? 0 : s == GameStatus::Scheduled ? 1 : 2; };
    std::sort(out.begin(), out.end(), [&](const Game& a, const Game& b) {
        if (rank(a.status) != rank(b.status)) return rank(a.status) < rank(b.status);
        return a.start_epoch < b.start_epoch;
    });
    return out;
}

std::string games_json(const std::vector<Game>& games)
{
    return games_json(games, {});
}

std::string games_json(const std::vector<Game>& games, const std::map<std::string, Game>& last_results)
{
    JsonDocument doc;
    JsonArray arr = doc["games"].to<JsonArray>();
    auto add = [](JsonArray& a, const Game& g) {
        JsonObject o = a.add<JsonObject>();
        o["league"] = g.league;
        o["home"] = g.home.name;
        o["away"] = g.away.name;
        o["home_score"] = g.home.score;
        o["away_score"] = g.away.score;
        o["status"] = game_status_name(g.status);
        o["clock"] = g.clock;
        o["start_epoch"] = g.start_epoch;
    };
    for (const auto& g : games) add(arr, g);
    if (!last_results.empty()) {
        JsonArray last = doc["last_results"].to<JsonArray>();
        for (const auto& [key, g] : last_results) {
            add(last, g);
            last[last.size() - 1]["team"] = key;
        }
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace chan
