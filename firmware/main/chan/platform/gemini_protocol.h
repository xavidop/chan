// Gemini Live API as a xiaozhi Protocol: the device talks straight to Google over a TLS
// WebSocket, sends raw 16 kHz PCM from the mic and plays the model's 24 kHz PCM reply.
// The xiaozhi Application keeps driving the state machine; this class translates
// Gemini's events into the small JSON vocabulary it already understands
// ("tts start/stop", "stt", …).
#pragma once

#include <chan/core/gemini_messages.h>
#include <protocol.h>
#include <web_socket.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace chan::platform {

class GeminiProtocol : public Protocol {
public:
    GeminiProtocol();
    ~GeminiProtocol() override;

    bool Start() override { return true; }
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    AudioWire audio_wire() const override { return AudioWire::Pcm16; }

    // Gemini does its own turn detection; these xiaozhi hints are no-ops here.
    void SendWakeWordDetected(const std::string&) override {}
    void SendStartListening(ListeningMode) override {}
    void SendStopListening() override {}
    void SendMcpMessage(const std::string&) override {}
    // Local barge-in (wake word while speaking): drop what is left of the reply.
    void SendAbortSpeaking(AbortReason reason) override;

    // Sends one JPEG frame into the live session (the `look` tool).
    bool SendImage(const uint8_t* jpeg, size_t len);

    // Set by ChanApp: tool call → response JSON object (string). Runs on the receive task.
    std::function<std::string(const ServerMsg::Call&)> on_tool_call;
    // Approximate loudness (0..1) of the reply being played, for the LEDs.
    std::function<void(float)> on_output_level;

private:
    bool SendText(const std::string& text) override;
    void HandleServerMessage(std::string_view json);
    void EmitTts(const char* state, const std::string* text = nullptr);
    void EmitStt(const std::string& text);
    void FlushLocalPlayback();
    static void FeederTask(void* arg);
    void FeedOne(std::vector<uint8_t> pcm);
    void CheckIdle();

    // websocket_ is touched from three tasks: the audio task (SendAudio → CheckIdle → close), the
    // socket's own receive task (tool responses) and the Application loop (close). The pointer is
    // only read or swapped under ws_mutex_; the socket itself is destroyed outside the lock,
    // because destroying it joins the receive task, which may be waiting for that same lock.
    mutable std::mutex ws_mutex_;
    std::unique_ptr<WebSocket> websocket_;
    std::unique_ptr<WebSocket> TakeWebSocket();
    EventGroupHandle_t events_;
    SemaphoreHandle_t audio_available_;
    TaskHandle_t feeder_ = nullptr;
    std::mutex queue_mutex_;
    std::deque<std::vector<uint8_t>> audio_queue_;
    std::atomic<bool> opened_{false};
    std::atomic<bool> speaking_{false};
    std::atomic<bool> go_away_{false};
    std::atomic<uint32_t> last_activity_ms_{0};
    int debug_msgs_left_ = 0;  // log the first few server messages of a session
    uint32_t sent_packets_ = 0, sent_bytes_ = 0, recv_msgs_ = 0, last_stats_ms_ = 0;
    std::string output_text_;
    std::string input_text_;
};

}  // namespace chan::platform
