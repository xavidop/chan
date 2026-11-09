#include "leds.h"

#include <hal/hal.h>

#include <array>
#include <cmath>

namespace chan::platform {

namespace {

constexpr int kLedCount = 12;
constexpr float kMaxBrightness = 0.55f;  // the strip is bright; keep it cosy
constexpr float kDimFactor = 0.25f;      // quiet hours
constexpr uint32_t kSoftGlowColor = 0x30261A;

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

Rgb scale(uint32_t hex, float k)
{
    k = std::fmax(0.0f, std::fmin(1.0f, k));
    return Rgb{static_cast<uint8_t>(((hex >> 16) & 0xFF) * k), static_cast<uint8_t>(((hex >> 8) & 0xFF) * k),
               static_cast<uint8_t>((hex & 0xFF) * k)};
}

uint32_t hsv_to_hex(float h /*0..1*/, float s, float v)
{
    float r, g, b;
    int i = static_cast<int>(h * 6.0f) % 6;
    float f = h * 6.0f - std::floor(h * 6.0f);
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
    return (static_cast<uint32_t>(r * 255) << 16) | (static_cast<uint32_t>(g * 255) << 8) | static_cast<uint32_t>(b * 255);
}

float wave01(uint32_t now_ms, uint32_t period_ms)
{
    const float ph = static_cast<float>(now_ms % period_ms) / period_ms;
    return 0.5f - 0.5f * std::cos(ph * 2.0f * static_cast<float>(M_PI));
}

std::array<Rgb, kLedCount> g_last{};
bool g_has_last = false;

}  // namespace

void leds_apply(const Output& out, uint32_t now_ms, float voice_level)
{
    std::array<Rgb, kLedCount> frame{};
    const float master = kMaxBrightness * (out.dim ? kDimFactor : 1.0f);

    switch (out.led) {
        case LedPattern::Off:
            break;
        case LedPattern::SoftGlow:
            frame.fill(scale(kSoftGlowColor, master));
            break;
        case LedPattern::Pulse:
            frame.fill(scale(out.color, master * (0.3f + 0.7f * wave01(now_ms, 1500))));
            break;
        case LedPattern::Breathe:
            frame.fill(scale(out.color, master * (0.08f + 0.92f * wave01(now_ms, 2400))));
            break;
        case LedPattern::Blink:
            if (now_ms % 1000 < 300) frame.fill(scale(out.color, master));
            break;
        case LedPattern::DimFlash:
            if (now_ms % 400 < 200) frame.fill(scale(out.color, master * 0.35f));
            break;
        case LedPattern::TeamFlash:
            frame.fill(scale((now_ms % 500 < 250) ? out.color : (out.alt_color ? out.alt_color : 0xFFFFFF), master));
            break;
        case LedPattern::Chase: {
            const int head = static_cast<int>((now_ms / 100) % kLedCount);
            for (int i = 0; i < kLedCount; ++i) {
                int d = (head - i + kLedCount) % kLedCount;  // distance behind the head
                float k = d == 0 ? 1.0f : d == 1 ? 0.45f : d == 2 ? 0.15f : 0.03f;
                frame[i] = scale(out.color, master * k);
            }
            break;
        }
        case LedPattern::VoiceLevel: {
            float level = voice_level < 0 ? 0.35f + 0.65f * wave01(now_ms, 300) : voice_level;
            frame.fill(scale(out.color, master * (0.15f + 0.85f * level)));
            break;
        }
        case LedPattern::Rainbow:
            for (int i = 0; i < kLedCount; ++i) {
                float h = std::fmod(static_cast<float>(now_ms) / 1200.0f + static_cast<float>(i) / kLedCount, 1.0f);
                frame[i] = scale(hsv_to_hex(h, 1.0f, 1.0f), master);
            }
            break;
    }

    if (g_has_last && frame == g_last) return;
    g_last = frame;
    g_has_last = true;
    auto& hal = GetHAL();
    for (int i = 0; i < kLedCount; ++i) hal.setRgbColor(static_cast<uint8_t>(i), frame[i].r, frame[i].g, frame[i].b);
    hal.refreshRgb();
}

}  // namespace chan::platform
