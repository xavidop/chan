// Attention: turns what is happening (listening, agent needs you, goal!) into one
// coherent output for the LEDs, the face and the speaker. Pure C++17, host-tested.
//
// Priority, high → low: Celebrate/Sulk (timed) > AgentAlert > Listening > Speaking >
// Thinking > Offline > idle. Sounds are queued (a small bounded FIFO) when a signal is
// raised and handed out one at a time by take_sound(); quiet hours swallow them.
#pragma once

#include <cstdint>

namespace chan {

enum class Signal { Offline, Listening, Thinking, Speaking, AgentAlert, Celebrate, Sulk, Count_ };

enum class LedPattern { Off, SoftGlow, Pulse, Chase, VoiceLevel, Blink, Breathe, TeamFlash, Rainbow, DimFlash };

enum class Expression { Neutral, Attentive, Thinking, Talking, Confused, Worried, Happy, Sulky };

enum class Sound { None, Chime, Nudge, Cheer, GoalHorn, Aww };

struct Output {
    LedPattern led = LedPattern::SoftGlow;
    uint32_t color = 0;      // 0xRRGGBB, pattern-specific
    uint32_t alt_color = 0;  // second colour for TeamFlash
    Expression face = Expression::Neutral;
    Sound sound = Sound::None;  // informational; use take_sound() to consume
    bool dim = false;           // quiet hours: dim LEDs
};

class Attention {
public:
    static constexpr uint32_t kDefaultTimedMs = 8000;
    // Sounds waiting to be played; a goal and an agent alert in the same tick both play.
    static constexpr int kMaxPendingSounds = 3;
    static constexpr uint32_t kDefaultAgentColor = 0xFFA000;
    static constexpr uint32_t kListeningColor = 0x00C8FF;
    static constexpr uint32_t kThinkingColor = 0x2050FF;
    static constexpr uint32_t kOfflineColor = 0xFF2020;
    static constexpr uint32_t kSulkColor = 0xFF2020;

    // Raise a signal at `now_ms`. `color`/`alt` are used by AgentAlert (agent colour) and
    // Celebrate (team colours). `duration_ms` applies to Celebrate/Sulk (0 → 8000 ms).
    // Raising a timed signal that is already active restarts it. Sound-bearing signals
    // (AgentAlert → Chime, Celebrate → Cheer, Sulk → Aww) queue their sound.
    void raise(Signal s, uint32_t now_ms, uint32_t color = 0, uint32_t alt = 0, uint32_t duration_ms = 0);
    void clear(Signal s);
    // Head pat / voice turn: drop AgentAlert, Celebrate and Sulk. Listening/Offline stay.
    void acknowledge();
    // Queue a sound explicitly (e.g. the agent re-nudge). Dropped when the queue is full.
    void queue_sound(Sound s);

    Output output(uint32_t now_ms, bool quiet) const;
    // Returns the oldest pending sound, or None. Always None while quiet (and drops them all).
    Sound take_sound(bool quiet);

    bool is_active(Signal s, uint32_t now_ms) const;

private:
    struct State {
        bool active = false;
        uint32_t raised_ms = 0;
        uint32_t until_ms = 0;  // 0 = until cleared
        uint32_t color = 0, alt = 0;
    };
    State _s[static_cast<int>(Signal::Count_)];
    Sound _pending[kMaxPendingSounds] = {};
    int _pending_count = 0;

    const State& st(Signal s) const { return _s[static_cast<int>(s)]; }
    State& st(Signal s) { return _s[static_cast<int>(s)]; }
};

}  // namespace chan
