#include <doctest.h>

#include <chan/core/attention.h>
#include <chan/core/quiet_hours.h>

using namespace chan;

TEST_CASE("quiet hours wrap midnight")
{
    QuietHours q{true, 23 * 60, 7 * 60};
    CHECK(in_quiet_hours(q, 1380));  // 23:00
    CHECK(in_quiet_hours(q, 0));     // 00:00
    CHECK(in_quiet_hours(q, 419));   // 06:59
    CHECK_FALSE(in_quiet_hours(q, 420));  // 07:00
    CHECK_FALSE(in_quiet_hours(q, 720));  // 12:00
    CHECK_FALSE(in_quiet_hours(q, 1379)); // 22:59
}

TEST_CASE("quiet hours same-day window")
{
    QuietHours q{true, 13 * 60, 15 * 60};
    CHECK(in_quiet_hours(q, 13 * 60));
    CHECK(in_quiet_hours(q, 14 * 60));
    CHECK_FALSE(in_quiet_hours(q, 15 * 60));
    CHECK_FALSE(in_quiet_hours(q, 9 * 60));
}

TEST_CASE("disabled quiet hours never quiet")
{
    QuietHours q{false, 0, 24 * 60 - 1};
    CHECK_FALSE(in_quiet_hours(q, 0));
    CHECK_FALSE(in_quiet_hours(q, 720));
}

TEST_CASE("idle output is soft glow, off when quiet")
{
    Attention a;
    Output o = a.output(1000, false);
    CHECK(o.led == LedPattern::SoftGlow);
    CHECK(o.face == Expression::Neutral);
    CHECK_FALSE(o.dim);
    Output q = a.output(1000, true);
    CHECK(q.led == LedPattern::Off);
    CHECK(q.dim);
}

TEST_CASE("listening thinking speaking map to spec colours")
{
    Attention a;
    a.raise(Signal::Listening, 0);
    Output o = a.output(10, false);
    CHECK(o.led == LedPattern::Pulse);
    CHECK(o.color == 0x00C8FF);
    CHECK(o.face == Expression::Attentive);

    a.clear(Signal::Listening);
    a.raise(Signal::Thinking, 20);
    o = a.output(30, false);
    CHECK(o.led == LedPattern::Chase);
    CHECK(o.color == 0x2050FF);
    CHECK(o.face == Expression::Thinking);

    a.clear(Signal::Thinking);
    a.raise(Signal::Speaking, 40);
    o = a.output(50, false);
    CHECK(o.led == LedPattern::VoiceLevel);
    CHECK(o.face == Expression::Talking);
}

TEST_CASE("offline outranks idle but not agent alert")
{
    Attention a;
    a.raise(Signal::Offline, 0);
    Output o = a.output(10, false);
    CHECK(o.led == LedPattern::Blink);
    CHECK(o.color == 0xFF2020);
    CHECK(o.face == Expression::Confused);

    a.raise(Signal::AgentAlert, 20, 0xD97757);
    o = a.output(30, false);
    CHECK(o.led == LedPattern::Breathe);
    CHECK(o.color == 0xD97757);
    CHECK(o.face == Expression::Worried);

    a.clear(Signal::Offline);
    o = a.output(40, false);
    CHECK(o.led == LedPattern::Breathe);
}

TEST_CASE("celebrate outranks agent alert then expires after 8000ms")
{
    Attention a;
    a.raise(Signal::AgentAlert, 0, 0x10A37F);
    a.raise(Signal::Celebrate, 1000, 0xEE7203, 0x1A1A1A);
    Output o = a.output(1500, false);
    CHECK(o.led == LedPattern::TeamFlash);
    CHECK(o.color == 0xEE7203);
    CHECK(o.alt_color == 0x1A1A1A);
    CHECK(o.face == Expression::Happy);
    // second half of the celebration goes rainbow
    o = a.output(1000 + 5000, false);
    CHECK(o.led == LedPattern::Rainbow);
    CHECK(o.face == Expression::Happy);
    // expired: agent alert shows again
    o = a.output(1000 + 8000, false);
    CHECK(o.led == LedPattern::Breathe);
    CHECK(o.color == 0x10A37F);
}

TEST_CASE("sulk is a short dim red")
{
    Attention a;
    a.raise(Signal::Sulk, 0);
    Output o = a.output(100, false);
    CHECK(o.led == LedPattern::DimFlash);
    CHECK(o.color == 0xFF2020);
    CHECK(o.face == Expression::Sulky);
    o = a.output(8000, false);
    CHECK(o.led == LedPattern::SoftGlow);
}

TEST_CASE("custom duration is honoured")
{
    Attention a;
    a.raise(Signal::Celebrate, 0, 0x112233, 0, 2000);
    CHECK(a.output(1999, false).led != LedPattern::SoftGlow);
    CHECK(a.output(2000, false).led == LedPattern::SoftGlow);
}

TEST_CASE("sound fires once")
{
    Attention a;
    a.raise(Signal::Celebrate, 0, 0xEE7203);
    CHECK(a.take_sound(false) == Sound::Cheer);
    CHECK(a.take_sound(false) == Sound::None);
    a.raise(Signal::AgentAlert, 10, 0xD97757);
    CHECK(a.take_sound(false) == Sound::Chime);
    CHECK(a.take_sound(false) == Sound::None);
    a.raise(Signal::Sulk, 20);
    CHECK(a.take_sound(false) == Sound::Aww);
}

TEST_CASE("no sound when quiet but alert still shown")
{
    Attention a;
    a.raise(Signal::AgentAlert, 0, 0xD97757);
    CHECK(a.take_sound(true) == Sound::None);
    Output o = a.output(10, true);
    CHECK(o.led == LedPattern::Breathe);
    CHECK(o.face == Expression::Worried);
    CHECK(o.dim);
}

TEST_CASE("listening does not play a sound")
{
    Attention a;
    a.raise(Signal::Listening, 0);
    CHECK(a.take_sound(false) == Sound::None);
}

TEST_CASE("acknowledge clears alert and celebration")
{
    Attention a;
    a.raise(Signal::AgentAlert, 0, 0xD97757);
    a.raise(Signal::Celebrate, 0, 0xEE7203);
    a.acknowledge();
    Output o = a.output(10, false);
    CHECK(o.led == LedPattern::SoftGlow);
    CHECK(o.face == Expression::Neutral);
}

TEST_CASE("acknowledge keeps listening and offline")
{
    Attention a;
    a.raise(Signal::Offline, 0);
    a.raise(Signal::Listening, 0);
    a.acknowledge();
    CHECK(a.output(10, false).led == LedPattern::Pulse);
    a.clear(Signal::Listening);
    CHECK(a.output(20, false).led == LedPattern::Blink);
}

TEST_CASE("raising an active timed signal restarts it")
{
    Attention a;
    a.raise(Signal::Celebrate, 0, 0xEE7203);
    a.raise(Signal::Celebrate, 6000, 0xEE7203);
    CHECK(a.output(9000, false).led != LedPattern::SoftGlow);
    CHECK(a.output(14000, false).led == LedPattern::SoftGlow);
}

TEST_CASE("agent colour fallback when none given")
{
    Attention a;
    a.raise(Signal::AgentAlert, 0);
    CHECK(a.output(1, false).color == 0xFFA000);
}

TEST_CASE("agent done is a short pulse in the agent colour with a happy face")
{
    Attention a;
    a.raise(Signal::AgentDone, 0, 0x10A37F);
    Output o = a.output(100, false);
    CHECK(o.led == LedPattern::Pulse);
    CHECK(o.color == 0x10A37F);
    CHECK(o.face == Expression::Happy);
    CHECK(a.take_sound(false) == Sound::None);  // lights only, no chime
    // over after 4 s
    o = a.output(4000, false);
    CHECK(o.led == LedPattern::SoftGlow);
    CHECK(o.face == Expression::Neutral);
}

TEST_CASE("agent done falls back to the default agent colour")
{
    Attention a;
    a.raise(Signal::AgentDone, 0);
    CHECK(a.output(10, false).color == Attention::kDefaultAgentColor);
}

TEST_CASE("agent done yields to an alert and to a conversation but outranks offline")
{
    Attention a;
    a.raise(Signal::Offline, 0);
    a.raise(Signal::AgentDone, 0, 0x10A37F);
    CHECK(a.output(10, false).led == LedPattern::Pulse);
    a.raise(Signal::Listening, 20);
    CHECK(a.output(30, false).color == Attention::kListeningColor);
    a.clear(Signal::Listening);
    a.raise(Signal::AgentAlert, 40, 0xD97757);
    CHECK(a.output(50, false).led == LedPattern::Breathe);
    a.acknowledge();  // a head pat ends the done pulse too
    CHECK(a.output(60, false).led == LedPattern::Blink);
}
