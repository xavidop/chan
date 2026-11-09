// Attention: turns what is happening (listening, agent needs you, goal!) into one
// coherent output for the LEDs, the face and the speaker. Pure C++17, host-tested.
//
// Priority, high → low: Celebrate/Sulk (timed) > AgentAlert > Listening > Speaking >
// Thinking > AgentDone (timed, lights only) > Offline > idle. Sounds are queued when a
// signal is raised and handed out once by take_sound(); quiet hours swallow them.
#pragma once

#include <cstdint>

namespace chan {

enum class Signal { Offline, Listening, Thinking, Speaking, AgentAlert, AgentDone, Celebrate, Sulk, Count_ };

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
    static constexpr uint32_t kAgentDoneMs = 4000;  // "your agent finished" pulse
    static constexpr uint32_t kDefaultAgentColor = 0xFFA000;
    static constexpr uint32_t kListeningColor = 0x00C8FF;
    static constexpr uint32_t kThinkingColor = 0x2050FF;
    static constexpr uint32_t kOfflineColor = 0xFF2020;
    static constexpr uint32_t kSulkColor = 0xFF2020;

    // Raise a signal at `now_ms`. `color`/`alt` are used by AgentAlert/AgentDone (agent colour)
    // and Celebrate (team colours). `duration_ms` applies to the timed signals (0 → default:
    // 8000 ms for Celebrate/Sulk, 4000 ms for AgentDone). Raising a timed signal that is already
    // active restarts it. Sound-bearing signals (AgentAlert → Chime, Celebrate → Cheer,
    // Sulk → Aww) queue their sound; AgentDone is lights only.
    void raise(Signal s, uint32_t now_ms, uint32_t color = 0, uint32_t alt = 0, uint32_t duration_ms = 0);
    void clear(Signal s);
    // Head pat / voice turn: drop AgentAlert, AgentDone, Celebrate and Sulk. Listening/Offline stay.
    void acknowledge();
    // Queue a sound explicitly (e.g. the agent re-nudge).
    void queue_sound(Sound s);

    Output output(uint32_t now_ms, bool quiet) const;
    // Returns the pending sound once, or None. Always None while quiet (and drops it).
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
    Sound _pending = Sound::None;

    const State& st(Signal s) const { return _s[static_cast<int>(s)]; }
    State& st(Signal s) { return _s[static_cast<int>(s)]; }
};

}  // namespace chan
