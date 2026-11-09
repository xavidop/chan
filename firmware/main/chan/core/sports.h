// Live scores for followed teams: ESPN (soccer, NFL, NBA, NHL) and MLB Stats API parsers, the
// score-change detector that fires celebrations, and "last result" memory. Pure C++17, host-tested.
#pragma once

#include <ArduinoJson.h>
#include <chan/core/config.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace chan {

enum class Sport { Soccer, Football, Basketball, Hockey, Baseball };
Sport sport_for_league(std::string_view league);  // nfl, nba, nhl, mlb; anything else is soccer

enum class GameStatus { Scheduled, Live, Final };
const char* game_status_name(GameStatus s);

struct Side {
    std::string name;    // display name, e.g. "Toronto Blue Jays"
    std::string abbrev;  // e.g. "TOR" (ESPN only)
    int score = 0;
};

struct Game {
    std::string id, league;
    Side home, away;
    GameStatus status = GameStatus::Scheduled;
    std::string clock;  // "62'", "Q3 05:12", "Top 7", "Final"
    uint64_t start_epoch = 0;  // UTC seconds
};

// Request URLs. ESPN: today's scoreboard, or a specific day (YYYYMMDD) for look-back.
std::string espn_scoreboard_url(const std::string& league, const std::string& yyyymmdd = "");
std::string mlb_schedule_url(const std::string& yyyy_mm_dd);

// ArduinoJson filters that keep only the fields the parsers read.
const JsonDocument& espn_filter();
const JsonDocument& mlb_filter();

// Parse from an already-deserialized document (what the device does, no second copy) or from text.
std::vector<Game> parse_espn_scoreboard(JsonVariantConst root, const std::string& league);
std::vector<Game> parse_mlb_schedule(JsonVariantConst root);
std::vector<Game> parse_espn_scoreboard(std::string_view json, const std::string& league);
std::vector<Game> parse_mlb_schedule(std::string_view json);

uint64_t parse_iso8601_utc(std::string_view text);  // "2026-11-06T20:00Z" / "…T00:08:00Z" → epoch, 0 if invalid

// "Last match" fallback: resolve a followed team's id from the league's team list, then read its
// season schedule (both ESPN and MLB). Payloads are moderate (≈50–250 KB) and only fetched when
// the daily look-back found no recent game for that team.
struct TeamRef {
    std::string id, name, abbrev;
};
std::string espn_teams_url(const std::string& league);
std::string espn_team_schedule_url(const std::string& league, const std::string& team_id);
std::string mlb_teams_url();
std::string mlb_team_schedule_url(const std::string& team_id, int season);
const JsonDocument& espn_teams_filter();
const JsonDocument& mlb_teams_filter();
std::vector<TeamRef> parse_espn_teams(JsonVariantConst root);
std::vector<TeamRef> parse_mlb_teams(JsonVariantConst root);
std::vector<TeamRef> parse_espn_teams(std::string_view json);
std::vector<TeamRef> parse_mlb_teams(std::string_view json);
std::string find_team_id(const std::vector<TeamRef>& refs, const Team& team);  // "" when not found

bool game_involves(const Game& g, const Team& t);  // same league; name/display name/abbrev, case-insensitive
const Team* followed_side(const Game& g, bool home, const std::vector<Team>& teams);

struct ScoreEvent {
    std::string game_id, team_key;  // team_key: the followed team this event is about
    bool ours = false;              // true: the followed team scored; false: its opponent did
    int points = 0;
    Sport sport = Sport::Soccer;
    std::string line;  // "GOAL!", "TOUCHDOWN!", …
};
std::string score_line(Sport sport, int points);

class ScoreTracker {
public:
    // Feed a fresh snapshot. `fetched_leagues` are the leagues whose fetch succeeded this round:
    // their previously tracked games that are no longer listed are dropped (so nothing stays "live"
    // forever and memory stays bounded); leagues not in the set keep their last snapshot.
    // Only games involving a followed team are retained. Returns scoring events for followed teams
    // since the previous snapshot of each game; first observation of a game never fires.
    std::vector<ScoreEvent> update(const std::vector<Game>& games, const std::vector<Team>& followed,
                                   const std::set<std::string>& fetched_leagues);
    // Convenience: fetched_leagues = the leagues present in `games`.
    std::vector<ScoreEvent> update(const std::vector<Game>& games, const std::vector<Team>& followed);
    size_t tracked_count() const { return _games.size(); }
    // Games in the current snapshots that involve a followed team (live first, then scheduled, then final).
    std::vector<Game> followed_games() const;
    // Latest final game seen per followed team key (from live polls or historical look-back).
    std::map<std::string, Game> last_results() const { return _last_results; }
    void remember_results(const std::vector<Game>& games, const std::vector<Team>& followed);

private:
    std::map<std::string, Game> _games;         // by game id
    std::map<std::string, Game> _last_results;  // by team key
    std::vector<Team> _followed;
};

std::string games_json(const std::vector<Game>& games);
std::string games_json(const std::vector<Game>& games, const std::map<std::string, Game>& last_results);

}  // namespace chan
