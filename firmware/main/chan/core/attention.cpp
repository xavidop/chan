#include "attention.h"

namespace chan {

namespace {
bool is_timed(Signal s)
{
    return s == Signal::Celebrate || s == Signal::Sulk || s == Signal::AgentDone;
}
}  // namespace

void Attention::raise(Signal s, uint32_t now_ms, uint32_t color, uint32_t alt, uint32_t duration_ms)
{
    State& x = st(s);
    x.active = true;
    x.raised_ms = now_ms;
    x.color = color;
    x.alt = alt;
    const uint32_t default_ms = s == Signal::AgentDone ? kAgentDoneMs : kDefaultTimedMs;
    x.until_ms = is_timed(s) ? now_ms + (duration_ms ? duration_ms : default_ms) : 0;

    switch (s) {
        case Signal::AgentAlert: _pending = Sound::Chime; break;
        case Signal::Celebrate: _pending = Sound::Cheer; break;
        case Signal::Sulk: _pending = Sound::Aww; break;
        default: break;
    }
}

void Attention::clear(Signal s)
{
    st(s).active = false;
}

void Attention::acknowledge()
{
    clear(Signal::AgentAlert);
    clear(Signal::AgentDone);
    clear(Signal::Celebrate);
    clear(Signal::Sulk);
}

void Attention::queue_sound(Sound s)
{
    _pending = s;
}

bool Attention::is_active(Signal s, uint32_t now_ms) const
{
    const State& x = st(s);
    if (!x.active) return false;
    if (x.until_ms != 0 && now_ms >= x.until_ms) return false;
    return true;
}

Output Attention::output(uint32_t now_ms, bool quiet) const
{
    Output o;
    o.dim = quiet;

    if (is_active(Signal::Celebrate, now_ms)) {
        const State& x = st(Signal::Celebrate);
        const uint32_t total = x.until_ms - x.raised_ms;
        const bool first_half = (now_ms - x.raised_ms) * 2 < total;
        o.led = first_half ? LedPattern::TeamFlash : LedPattern::Rainbow;
        o.color = x.color;
        o.alt_color = x.alt;
        o.face = Expression::Happy;
        return o;
    }
    if (is_active(Signal::Sulk, now_ms)) {
        o.led = LedPattern::DimFlash;
        o.color = kSulkColor;
        o.face = Expression::Sulky;
        return o;
    }
    if (is_active(Signal::AgentAlert, now_ms)) {
        const State& x = st(Signal::AgentAlert);
        o.led = LedPattern::Breathe;
        o.color = x.color ? x.color : kDefaultAgentColor;
        o.face = Expression::Worried;
        return o;
    }
    if (is_active(Signal::Listening, now_ms)) {
        o.led = LedPattern::Pulse;
        o.color = kListeningColor;
        o.face = Expression::Attentive;
        return o;
    }
    if (is_active(Signal::Speaking, now_ms)) {
        o.led = LedPattern::VoiceLevel;
        o.color = kListeningColor;
        o.face = Expression::Talking;
        return o;
    }
    if (is_active(Signal::Thinking, now_ms)) {
        o.led = LedPattern::Chase;
        o.color = kThinkingColor;
        o.face = Expression::Thinking;
        return o;
    }
    if (is_active(Signal::AgentDone, now_ms)) {
        const State& x = st(Signal::AgentDone);
        o.led = LedPattern::Pulse;
        o.color = x.color ? x.color : kDefaultAgentColor;
        o.face = Expression::Happy;
        return o;
    }
    if (is_active(Signal::Offline, now_ms)) {
        o.led = LedPattern::Blink;
        o.color = kOfflineColor;
        o.face = Expression::Confused;
        return o;
    }
    o.led = quiet ? LedPattern::Off : LedPattern::SoftGlow;
    o.face = Expression::Neutral;
    return o;
}

Sound Attention::take_sound(bool quiet)
{
    Sound s = _pending;
    _pending = Sound::None;
    return quiet ? Sound::None : s;
}

}  // namespace chan
