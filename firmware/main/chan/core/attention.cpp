#include "attention.h"

namespace chan {

namespace {
bool is_timed(Signal s)
{
    return s == Signal::Celebrate || s == Signal::Sulk;
}
}  // namespace

void Attention::raise(Signal s, uint32_t now_ms, uint32_t color, uint32_t alt, uint32_t duration_ms)
{
    State& x = st(s);
    x.active = true;
    x.raised_ms = now_ms;
    x.color = color;
    x.alt = alt;
    x.until_ms = is_timed(s) ? now_ms + (duration_ms ? duration_ms : kDefaultTimedMs) : 0;

    switch (s) {
        case Signal::AgentAlert: queue_sound(Sound::Chime); break;
        case Signal::Celebrate: queue_sound(Sound::Cheer); break;
        case Signal::Sulk: queue_sound(Sound::Aww); break;
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
    clear(Signal::Celebrate);
    clear(Signal::Sulk);
}

void Attention::queue_sound(Sound s)
{
    if (s == Sound::None || _pending_count >= kMaxPendingSounds) return;
    _pending[_pending_count++] = s;
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
    if (quiet) {
        _pending_count = 0;
        return Sound::None;
    }
    if (_pending_count == 0) return Sound::None;
    const Sound s = _pending[0];
    for (int i = 1; i < _pending_count; ++i) _pending[i - 1] = _pending[i];
    --_pending_count;
    return s;
}

}  // namespace chan
