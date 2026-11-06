#include <doctest.h>

#include <chan/core/sports.h>

#include "test_support.h"

using namespace chan;

namespace {
Team team(const char* key, const char* league, const char* name, const char* display = nullptr)
{
    Team t;
    t.key = key;
    t.league = league;
    t.name = name;
    t.display_name = display ? display : name;
    t.color = 0xEE7203;
    t.alt_color = 0x1A1A1A;
    return t;
}

Game game(const char* id, const char* league, const char* home, int hs, const char* away, int as, GameStatus st = GameStatus::Live)
{
    Game g;
    g.id = id;
    g.league = league;
    g.home = Side{home, "", hs};
    g.away = Side{away, "", as};
    g.status = st;
    return g;
}
}  // namespace

TEST_CASE("sport_for_league")
{
    CHECK(sport_for_league("nfl") == Sport::Football);
    CHECK(sport_for_league("nba") == Sport::Basketball);
    CHECK(sport_for_league("nhl") == Sport::Hockey);
    CHECK(sport_for_league("mlb") == Sport::Baseball);
    CHECK(sport_for_league("esp.1") == Sport::Soccer);
    CHECK(sport_for_league("uefa.champions") == Sport::Soccer);
}

TEST_CASE("parses real scoreboard fixture with filter")
{
    for (const char* name : {"espn_soccer.json", "espn_nfl.json", "espn_nba.json", "espn_nhl.json"}) {
        CAPTURE(name);
        std::string json = chan_test::fixture(name);
        // through the same filter the device uses
        JsonDocument doc;
        REQUIRE(deserializeJson(doc, json, DeserializationOption::Filter(espn_filter())) == DeserializationError::Ok);
        auto games = parse_espn_scoreboard(doc.as<JsonVariantConst>(), "test.league");
        REQUIRE(games.size() > 0);
        for (const auto& g : games) {
            CHECK_FALSE(g.id.empty());
            CHECK(g.league == "test.league");
            CHECK_FALSE(g.home.name.empty());
            CHECK_FALSE(g.away.name.empty());
            CHECK(g.home.score >= 0);
            CHECK(g.away.score >= 0);
            CHECK(g.start_epoch > 1700000000);
        }
    }
}

TEST_CASE("parses mlb fixture")
{
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, chan_test::fixture("mlb_schedule.json"), DeserializationOption::Filter(mlb_filter())) == DeserializationError::Ok);
    auto games = parse_mlb_schedule(doc.as<JsonVariantConst>());
    REQUIRE(games.size() > 0);
    CHECK(games[0].league == "mlb");
    CHECK_FALSE(games[0].home.name.empty());
    CHECK(games[0].start_epoch > 1700000000);
}

TEST_CASE("espn status and clock mapping")
{
    const char* json = R"({"events":[
      {"id":"1","date":"2026-11-06T20:00Z","shortName":"VAL @ RMA","status":{"type":{"state":"in","completed":false,"shortDetail":"62'"},"displayClock":"62'","period":2},
       "competitions":[{"competitors":[{"homeAway":"home","score":"2","team":{"displayName":"Real Madrid","abbreviation":"RMA"}},{"homeAway":"away","score":"1","team":{"displayName":"Valencia","abbreviation":"VAL"}}]}]},
      {"id":"2","date":"2026-11-07T15:00Z","shortName":"A @ B","status":{"type":{"state":"pre","completed":false,"shortDetail":"Sat 16:00"}},
       "competitions":[{"competitors":[{"homeAway":"home","score":"0","team":{"displayName":"B","abbreviation":"B"}},{"homeAway":"away","score":"0","team":{"displayName":"A","abbreviation":"A"}}]}]},
      {"id":"3","date":"2026-11-05T15:00Z","shortName":"C @ D","status":{"type":{"state":"post","completed":true,"shortDetail":"FT"}},
       "competitions":[{"competitors":[{"homeAway":"home","score":"3","team":{"displayName":"D","abbreviation":"D"}},{"homeAway":"away","score":"3","team":{"displayName":"C","abbreviation":"C"}}]}]}
    ]})";
    auto games = parse_espn_scoreboard(json, "esp.1");
    REQUIRE(games.size() == 3);
    CHECK(games[0].status == GameStatus::Live);
    CHECK(games[0].home.name == "Real Madrid");
    CHECK(games[0].home.score == 2);
    CHECK(games[0].away.abbrev == "VAL");
    CHECK(games[0].away.score == 1);
    CHECK(games[0].clock == "62'");
    CHECK(games[0].start_epoch == 1793995200);  // 2026-11-06T20:00Z
    CHECK(games[1].status == GameStatus::Scheduled);
    CHECK(games[2].status == GameStatus::Final);
}

TEST_CASE("mlb status and inning mapping")
{
    const char* json = R"({"dates":[{"games":[
      {"gamePk":1,"gameDate":"2026-10-25T00:08:00Z","status":{"abstractGameState":"Live"},
       "teams":{"home":{"score":4,"team":{"id":141,"name":"Toronto Blue Jays"}},"away":{"score":2,"team":{"id":119,"name":"Los Angeles Dodgers"}}},
       "linescore":{"currentInning":7,"inningState":"Top"}},
      {"gamePk":2,"gameDate":"2026-10-26T00:08:00Z","status":{"abstractGameState":"Preview"},
       "teams":{"home":{"team":{"id":1,"name":"X"}},"away":{"team":{"id":2,"name":"Y"}}}},
      {"gamePk":3,"gameDate":"2026-10-24T00:08:00Z","status":{"abstractGameState":"Final"},
       "teams":{"home":{"score":1,"team":{"id":1,"name":"X"}},"away":{"score":5,"team":{"id":2,"name":"Y"}}}}
    ]}]})";
    auto games = parse_mlb_schedule(json);
    REQUIRE(games.size() == 3);
    CHECK(games[0].id == "1");
    CHECK(games[0].status == GameStatus::Live);
    CHECK(games[0].home.name == "Toronto Blue Jays");
    CHECK(games[0].home.score == 4);
    CHECK(games[0].clock == "Top 7");
    CHECK(games[1].status == GameStatus::Scheduled);
    CHECK(games[1].home.score == 0);
    CHECK(games[2].status == GameStatus::Final);
}

TEST_CASE("game_involves matches name display name or abbrev case-insensitively")
{
    Game g = game("1", "mlb", "Toronto Blue Jays", 0, "Los Angeles Dodgers", 0);
    g.home.abbrev = "TOR";
    CHECK(game_involves(g, team("bluejays", "mlb", "Toronto Blue Jays", "Blue Jays")));
    CHECK(game_involves(g, team("tor", "mlb", "tor")));
    CHECK(game_involves(g, team("jays", "mlb", "toronto blue jays")));
    CHECK_FALSE(game_involves(g, team("other", "mlb", "Yankees")));
    CHECK_FALSE(game_involves(g, team("bluejays", "nba", "Toronto Blue Jays")));  // other league
}

TEST_CASE("no event on first observation")
{
    ScoreTracker t;
    auto ev = t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 1)}, {team("valencia", "esp.1", "Valencia")});
    CHECK(ev.empty());
}

TEST_CASE("our goal fires ours event with GOAL line")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 1)}, teams);
    auto ev = t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 2)}, teams);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].ours);
    CHECK(ev[0].team_key == "valencia");
    CHECK(ev[0].points == 1);
    CHECK(ev[0].sport == Sport::Soccer);
    CHECK(ev[0].line == "GOAL!");
}

TEST_CASE("opponent score fires sulk event")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 1)}, teams);
    auto ev = t.update({game("1", "esp.1", "Real Madrid", 3, "Valencia", 1)}, teams);
    REQUIRE(ev.size() == 1);
    CHECK_FALSE(ev[0].ours);
    CHECK(ev[0].team_key == "valencia");
}

TEST_CASE("no event on score decrease")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 2)}, teams);
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 1)}, teams).empty());  // correction
    // after the correction the new baseline is 1, so going back to 2 is a goal again
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 2)}, teams).size() == 1);
}

TEST_CASE("same score twice fires once")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("1", "esp.1", "Real Madrid", 0, "Valencia", 0)}, teams);
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 0, "Valencia", 1)}, teams).size() == 1);
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 0, "Valencia", 1)}, teams).empty());
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 0, "Valencia", 1)}, teams).empty());
}

TEST_CASE("unfollowed games never fire")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("9", "esp.1", "Sevilla", 0, "Betis", 0)}, teams);
    CHECK(t.update({game("9", "esp.1", "Sevilla", 1, "Betis", 0)}, teams).empty());
}

TEST_CASE("touchdown is 6 points line TOUCHDOWN")
{
    ScoreTracker t;
    std::vector<Team> teams{team("raptors", "nba", "Toronto Raptors"), team("tfc", "nfl", "Toronto FC")};
    t.update({game("n", "nfl", "Toronto FC", 7, "Other", 0), game("b", "nba", "Toronto Raptors", 90, "Other", 88)}, teams);
    auto ev = t.update({game("n", "nfl", "Toronto FC", 13, "Other", 0), game("b", "nba", "Toronto Raptors", 93, "Other", 88)}, teams);
    REQUIRE(ev.size() == 2);
    const auto& nfl = ev[0].sport == Sport::Football ? ev[0] : ev[1];
    const auto& nba = ev[0].sport == Sport::Basketball ? ev[0] : ev[1];
    CHECK(nfl.points == 6);
    CHECK(nfl.line == "TOUCHDOWN!");
    CHECK(nba.points == 3);
    CHECK(nba.line == "THREE!");
    CHECK(score_line(Sport::Football, 3) == "FIELD GOAL!");
    CHECK(score_line(Sport::Football, 2) == "SAFETY!");
    CHECK(score_line(Sport::Basketball, 2) == "BASKET!");
    CHECK(score_line(Sport::Basketball, 1) == "FREE THROW!");
    CHECK(score_line(Sport::Hockey, 1) == "GOAL!");
    CHECK(score_line(Sport::Baseball, 3) == "RUN!");
}

TEST_CASE("both sides followed scorer is ours")
{
    ScoreTracker t;
    std::vector<Team> teams{team("tor", "nba", "Toronto Raptors"), team("bos", "nba", "Boston Celtics")};
    t.update({game("b", "nba", "Toronto Raptors", 10, "Boston Celtics", 10)}, teams);
    auto ev = t.update({game("b", "nba", "Toronto Raptors", 10, "Boston Celtics", 12)}, teams);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].ours);
    CHECK(ev[0].team_key == "bos");
}

TEST_CASE("tracker keeps last games when a poll fails")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 1), game("9", "esp.1", "Sevilla", 0, "Betis", 0)}, teams);
    // a failed poll means update() is simply not called; the followed games are still there
    auto fg = t.followed_games();
    REQUIRE(fg.size() == 1);
    CHECK(fg[0].id == "1");
    // and the baseline survives, so the next real snapshot still detects the goal
    CHECK(t.update({game("1", "esp.1", "Real Madrid", 2, "Valencia", 2)}, teams).size() == 1);
}

TEST_CASE("team id lookup from the league team lists")
{
    auto espn = parse_espn_teams(chan_test::fixture("espn_teams.json"));
    CHECK(espn.size() >= 6);
    CHECK(find_team_id(espn, team("tfc", "usa.1", "Toronto FC")) == "");  // fixture keeps the first 6 teams only
    std::vector<TeamRef> refs{{"7318", "Toronto FC", "TOR"}, {"1", "Atlanta United FC", "ATL"}};
    CHECK(find_team_id(refs, team("tfc", "usa.1", "Toronto FC")) == "7318");
    CHECK(find_team_id(refs, team("tfc", "usa.1", "tor")) == "7318");
    CHECK(find_team_id(refs, team("x", "usa.1", "Nobody")) == "");

    auto mlb = parse_mlb_teams(chan_test::fixture("mlb_teams.json"));
    CHECK(find_team_id(mlb, team("bluejays", "mlb", "Toronto Blue Jays", "Blue Jays")) == "141");
    CHECK(find_team_id(mlb, team("jays", "mlb", "Blue Jays")) == "141");  // teamName / nickname
}

TEST_CASE("team schedule fixtures parse with object scores and competition-level status")
{
    auto games = parse_espn_scoreboard(chan_test::fixture("espn_team_schedule.json"), "usa.1");
    REQUIRE(games.size() == 3);  // the captured season had no upcoming game left
    int finals = 0;
    for (const auto& g : games) {
        if (g.status == GameStatus::Final) {
            ++finals;
            CHECK(g.home.score + g.away.score >= 0);
            CHECK(g.clock == "Final");
        }
        CHECK(g.start_epoch > 1700000000);
        CHECK_FALSE(g.home.name.empty());
    }
    CHECK(finals == 3);
    const char* obj = R"({"events":[{"id":"9","date":"2026-02-22T01:30Z","competitions":[{"status":{"type":{"state":"post","completed":true}},
      "competitors":[{"homeAway":"home","score":{"value":3.0,"displayValue":"3"},"team":{"displayName":"FC Dallas","abbreviation":"DAL"}},
                     {"homeAway":"away","score":{"value":1.0,"displayValue":"1"},"team":{"displayName":"Toronto FC","abbreviation":"TOR"}}]}]}]})";
    auto g = parse_espn_scoreboard(obj, "usa.1");
    REQUIRE(g.size() == 1);
    CHECK(g[0].home.score == 3);
    CHECK(g[0].away.score == 1);
    CHECK(g[0].status == GameStatus::Final);

    auto mlb = parse_mlb_schedule(chan_test::fixture("mlb_team_schedule.json"));
    REQUIRE(mlb.size() == 3);
    CHECK(mlb[2].status == GameStatus::Final);
    CHECK(mlb[2].home.name + mlb[2].away.name != "");
}

TEST_CASE("team schedule urls")
{
    CHECK(espn_teams_url("usa.1") == "http://site.api.espn.com/apis/site/v2/sports/soccer/usa.1/teams");
    CHECK(espn_team_schedule_url("nhl", "21") == "http://site.api.espn.com/apis/site/v2/sports/hockey/nhl/teams/21/schedule");
    CHECK(mlb_teams_url() == "http://statsapi.mlb.com/api/v1/teams?sportId=1");
    CHECK(mlb_team_schedule_url("141", 2026) == "http://statsapi.mlb.com/api/v1/schedule?sportId=1&teamId=141&season=2026&gameType=R,F,D,L,W");
}

TEST_CASE("last results keep the latest final game per team")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia"), team("bluejays", "mlb", "Toronto Blue Jays")};
    auto older = game("a", "esp.1", "Valencia", 1, "Sevilla", 0, GameStatus::Final);
    older.start_epoch = 1000;
    auto newer = game("b", "esp.1", "Betis", 2, "Valencia", 2, GameStatus::Final);
    newer.start_epoch = 2000;
    auto live = game("c", "esp.1", "Valencia", 0, "Getafe", 0, GameStatus::Live);
    live.start_epoch = 3000;
    t.remember_results({newer}, teams);
    t.remember_results({older}, teams);  // look-back arrives later but is older
    t.update({live}, teams);
    auto last = t.last_results();
    REQUIRE(last.count("valencia") == 1);
    CHECK(last["valencia"].id == "b");
    CHECK(last.count("bluejays") == 0);
    // a final game in a normal poll is remembered too
    auto done = game("c", "esp.1", "Valencia", 1, "Getafe", 0, GameStatus::Final);
    done.start_epoch = 3000;
    t.update({done}, teams);
    CHECK(t.last_results()["valencia"].id == "c");
}

TEST_CASE("urls")
{
    CHECK(espn_scoreboard_url("nfl") == "http://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard");
    CHECK(espn_scoreboard_url("esp.1", "20261105") == "http://site.api.espn.com/apis/site/v2/sports/soccer/esp.1/scoreboard?dates=20261105");
    CHECK(mlb_schedule_url("2026-11-05") == "http://statsapi.mlb.com/api/v1/schedule?sportId=1&date=2026-11-05&hydrate=linescore");
}

TEST_CASE("games json for the model")
{
    ScoreTracker t;
    std::vector<Team> teams{team("valencia", "esp.1", "Valencia")};
    auto g = game("1", "esp.1", "Real Madrid", 2, "Valencia", 1);
    g.clock = "62'";
    t.update({g}, teams);
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, games_json(t.followed_games())) == DeserializationError::Ok);
    REQUIRE(doc["games"].size() == 1);
    CHECK((doc["games"][0]["home"] == "Real Madrid"));
    CHECK((doc["games"][0]["away_score"] == 1));
    CHECK((doc["games"][0]["status"] == "live"));
    CHECK((doc["games"][0]["clock"] == "62'"));
}

TEST_CASE("iso8601 utc parsing")
{
    CHECK(parse_iso8601_utc("2026-11-06T20:00Z") == 1793995200);
    CHECK(parse_iso8601_utc("2026-10-25T00:08:00Z") == 1792886880);
    CHECK(parse_iso8601_utc("garbage") == 0);
}
