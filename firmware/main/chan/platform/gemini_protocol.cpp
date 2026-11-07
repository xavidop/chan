#include "gemini_protocol.h"

#include <application.h>
#include <board.h>
#include <chan/platform/chan_app.h>
#include <cJSON.h>
#include <esp_log.h>
#include <hal/hal.h>
#include <network_interface.h>

#include <cmath>

namespace chan::platform {

namespace {
constexpr const char* TAG = "GeminiLive";
constexpr EventBits_t kSetupCompleteBit = 1 << 0;
constexpr size_t kReceiveBufferBytes = 32 * 1024;
constexpr size_t kMaxQueuedChunks = 400;  // ~50 s of 24 kHz audio in 125 ms chunks
constexpr uint32_t kSetupTimeoutMs = 15000;

float rms_level(const std::vector<uint8_t>& pcm)
{
    const size_t n = pcm.size() / 2;
    if (n == 0) return 0.0f;
    double acc = 0;
    const auto* s = reinterpret_cast<const int16_t*>(pcm.data());
    for (size_t i = 0; i < n; i += 4) acc += static_cast<double>(s[i]) * s[i];
    const double rms = std::sqrt(acc / ((n + 3) / 4));
    return static_cast<float>(std::fmin(1.0, rms / 6000.0));
}
}  // namespace

GeminiProtocol::GeminiProtocol()
{
    events_ = xEventGroupCreate();
    audio_available_ = xSemaphoreCreateCounting(1024, 0);
    xTaskCreatePinnedToCore(FeederTask, "gemini_feed", 4096, this, 3, &feeder_, 0);
}

GeminiProtocol::~GeminiProtocol()
{
    CloseAudioChannel(false);
    if (feeder_) vTaskDelete(feeder_);
    vSemaphoreDelete(audio_available_);
    vEventGroupDelete(events_);
}

bool GeminiProtocol::OpenAudioChannel()
{
    auto& app = ChanApp::get();
    if (!app.hasConfig() || app.config().gemini_api_key.empty()) {
        SetError("Gemini API key missing", "set GEMINI_API_KEY in .env and run flash-config");
        return false;
    }
    if (app.isQuiet()) {
        ESP_LOGI(TAG, "quiet hours: not opening a voice session");
        return false;  // no alert: the Application just returns to idle
    }
    error_occurred_ = false;
    go_away_ = false;
    speaking_ = false;
    output_text_.clear();
    input_text_.clear();

    auto* network = Board::GetInstance().GetNetwork();
    std::unique_ptr<WebSocket> ws = network->CreateWebSocket(1);
    if (!ws) {
        SetError("Failed to create websocket");
        return false;
    }
    ws->SetReceiveBufferSize(kReceiveBufferBytes);
    ws->OnError([this](const NetworkError& error) {
        ESP_LOGE(TAG, "websocket error: %s", error.ToString().c_str());
    });
    ws->OnData([this](const char* data, size_t len, bool binary) {
        last_incoming_time_ = std::chrono::steady_clock::now();
        // The Live API sends its JSON messages as binary WebSocket frames; parse either kind.
        (void)binary;
        recv_msgs_++;
        if (debug_msgs_left_ > 0) {
            --debug_msgs_left_;
            ESP_LOGI(TAG, "<- %.*s", static_cast<int>(len > 120 ? 120 : len), data);
        }
        HandleServerMessage(std::string_view(data, len));
    });
    ws->OnDisconnected([this]() {
        ESP_LOGI(TAG, "disconnected");
        opened_ = false;
        speaking_ = false;
        if (on_audio_channel_closed_) on_audio_channel_closed_();
    });

    debug_msgs_left_ = 6;
    sent_packets_ = sent_bytes_ = recv_msgs_ = 0;
    last_stats_ms_ = GetHAL().millis();
    const std::string url = build_ws_url(app.config().gemini_api_key);
    ESP_LOGI(TAG, "connecting to wss://%s%s?key=***", kGeminiLiveHost, kGeminiLivePath);
    // Connect blocks for up to 10 s: do it before the socket is published to the other tasks.
    if (auto connected = ws->Connect(url.c_str()); !connected) {
        ESP_LOGE(TAG, "connect failed: %s", connected.error().ToString().c_str());
        SetError("Unable to reach Gemini", connected.error().ToString());
        return false;  // ws goes out of scope: disconnects and joins its receive task
    }
    xEventGroupClearBits(events_, kSetupCompleteBit);
    {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        websocket_ = std::move(ws);
    }

    if (!SendText(build_setup(app.buildGeminiSetup()))) {
        TakeWebSocket().reset();
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(events_, kSetupCompleteBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(kSetupTimeoutMs));
    if (!(bits & kSetupCompleteBit)) {
        ESP_LOGE(TAG, "no setupComplete within %u ms", static_cast<unsigned>(kSetupTimeoutMs));
        SetError("Gemini did not accept the session");
        if (auto dying = TakeWebSocket()) dying->Close();
        return false;
    }

    server_sample_rate_ = kGeminiOutputRate;
    server_frame_duration_ = 0;
    last_incoming_time_ = std::chrono::steady_clock::now();
    last_activity_ms_ = GetHAL().millis();
    opened_ = true;
    ESP_LOGI(TAG, "session open (model %s)", app.config().gemini_model.c_str());
    if (on_audio_channel_opened_) on_audio_channel_opened_();
    return true;
}

std::unique_ptr<WebSocket> GeminiProtocol::TakeWebSocket()
{
    std::lock_guard<std::mutex> lock(ws_mutex_);
    return std::move(websocket_);
}

void GeminiProtocol::CloseAudioChannel(bool)
{
    const bool was_open = opened_.exchange(false);
    speaking_ = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        audio_queue_.clear();
    }
    // Unpublish first (any SendText in flight finishes or sees null), then tear down unlocked:
    // the destructor joins the receive task, which must be free to return from SendText.
    if (auto dying = TakeWebSocket()) {
        dying->Close();  // OnDisconnected reports the closed channel
        dying.reset();
    } else if (was_open && on_audio_channel_closed_) {
        on_audio_channel_closed_();
    }
}

bool GeminiProtocol::IsAudioChannelOpened() const
{
    if (!opened_ || error_occurred_ || IsTimeout()) return false;
    std::lock_guard<std::mutex> lock(ws_mutex_);
    return websocket_ && websocket_->IsConnected();
}

bool GeminiProtocol::SendText(const std::string& text)
{
    std::lock_guard<std::mutex> lock(ws_mutex_);
    if (!websocket_ || !websocket_->IsConnected()) return false;
    if (!websocket_->Send(text)) {
        SetError("Failed to send to Gemini");
        return false;
    }
    return true;
}

bool GeminiProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet)
{
    if (!IsAudioChannelOpened()) return false;
    CheckIdle();
    if (!opened_) return false;
    const auto* pcm = reinterpret_cast<const int16_t*>(packet->payload.data());
    const bool ok = SendText(build_audio(pcm, packet->payload.size() / sizeof(int16_t)));
    sent_packets_++;
    sent_bytes_ += packet->payload.size();
    const uint32_t now = GetHAL().millis();
    if (now - last_stats_ms_ >= 5000) {
        last_stats_ms_ = now;
        ESP_LOGI(TAG, "mic → %u packets / %u bytes; voice=%d; idle %u ms; server msgs %u", static_cast<unsigned>(sent_packets_),
                 static_cast<unsigned>(sent_bytes_), Application::GetInstance().IsVoiceDetected() ? 1 : 0,
                 static_cast<unsigned>(now - last_activity_ms_), static_cast<unsigned>(recv_msgs_));
    }
    return ok;
}

bool GeminiProtocol::SendImage(const uint8_t* jpeg, size_t len)
{
    if (!IsAudioChannelOpened()) return false;
    return SendText(build_image(jpeg, len));
}

void GeminiProtocol::SendAbortSpeaking(AbortReason)
{
    FlushLocalPlayback();
    if (speaking_.exchange(false)) EmitTts("stop");
}

void GeminiProtocol::CheckIdle()
{
    auto& app = Application::GetInstance();
    const uint32_t now = GetHAL().millis();
    if (app.IsVoiceDetected() || speaking_) {
        last_activity_ms_ = now;
        return;
    }
    const uint32_t idle_ms = static_cast<uint32_t>(ChanApp::get().config().gemini_idle_s) * 1000;
    if (idle_ms > 0 && now - last_activity_ms_ > idle_ms) {
        ESP_LOGI(TAG, "idle for %u s, closing session", static_cast<unsigned>(idle_ms / 1000));
        CloseAudioChannel();
    }
}

void GeminiProtocol::EmitTts(const char* state, const std::string* text)
{
    if (!on_incoming_json_) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "tts");
    cJSON_AddStringToObject(root, "state", state);
    if (text) cJSON_AddStringToObject(root, "text", text->c_str());
    on_incoming_json_(root);
    cJSON_Delete(root);
}

void GeminiProtocol::EmitStt(const std::string& text)
{
    if (!on_incoming_json_) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "stt");
    cJSON_AddStringToObject(root, "text", text.c_str());
    on_incoming_json_(root);
    cJSON_Delete(root);
}

void GeminiProtocol::FlushLocalPlayback()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        audio_queue_.clear();
    }
    Application::GetInstance().GetAudioService().ResetDecoder();
    if (on_output_level) on_output_level(0.0f);
}

void GeminiProtocol::HandleServerMessage(std::string_view json)
{
    ServerMsg msg = parse_server_message(json);
    switch (msg.kind) {
        case ServerMsg::SetupComplete:
            xEventGroupSetBits(events_, kSetupCompleteBit);
            return;

        case ServerMsg::Audio: {
            last_activity_ms_ = GetHAL().millis();
            if (!speaking_.exchange(true)) {
                output_text_.clear();
                EmitTts("start");
            }
            if (!msg.output_text.empty()) {
                output_text_ += msg.output_text;
                EmitTts("sentence_start", &output_text_);
            }
            if (msg.sample_rate != kGeminiOutputRate) {
                ESP_LOGW(TAG, "unexpected audio rate %d, dropping chunk", msg.sample_rate);
                return;
            }
            bool dropped = false;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (audio_queue_.size() >= kMaxQueuedChunks) {
                    dropped = true;
                } else {
                    audio_queue_.push_back(std::move(msg.audio));
                }
            }
            if (dropped) {
                ESP_LOGW(TAG, "audio queue full, dropping chunk");
            } else {
                xSemaphoreGive(audio_available_);
            }
            return;
        }

        case ServerMsg::Transcription:
            last_activity_ms_ = GetHAL().millis();
            if (!msg.input_text.empty()) {
                input_text_ += msg.input_text;
                ESP_LOGI(TAG, ">> %s", input_text_.c_str());
                EmitStt(input_text_);
            }
            if (!msg.output_text.empty()) {
                output_text_ += msg.output_text;
                ESP_LOGI(TAG, "<< %s", output_text_.c_str());
                EmitTts("sentence_start", &output_text_);
            }
            return;

        case ServerMsg::Interrupted:
            ESP_LOGI(TAG, "interrupted by user");
            FlushLocalPlayback();
            if (speaking_.exchange(false)) EmitTts("stop");
            input_text_.clear();
            return;

        case ServerMsg::TurnComplete:
            if (speaking_.exchange(false)) EmitTts("stop");
            input_text_.clear();
            return;

        case ServerMsg::ToolCall:
            last_activity_ms_ = GetHAL().millis();
            for (const auto& call : msg.calls) {
                ESP_LOGI(TAG, "tool call: %s %s", call.name.c_str(), call.args_json.c_str());
                std::string response = on_tool_call ? on_tool_call(call) : std::string(R"({"error":"no tools available"})");
                SendText(build_tool_response(call.id, call.name, response));
            }
            return;

        case ServerMsg::GoAway:
            // Advisory: the server will end this connection later (it says how long is left).
            // Nothing to do now; when it drops, OnDisconnected returns the device to idle and
            // the next wake word opens a fresh session. Never close from this (receive) task.
            ESP_LOGI(TAG, "goAway received (session ends later)");
            go_away_ = true;
            return;

        case ServerMsg::Error:
            ESP_LOGW(TAG, "%s", msg.error.c_str());
            return;

        case ServerMsg::Unknown:
            return;
    }
}

void GeminiProtocol::FeederTask(void* arg)
{
    auto* self = static_cast<GeminiProtocol*>(arg);
    while (true) {
        if (xSemaphoreTake(self->audio_available_, portMAX_DELAY) != pdTRUE) continue;
        std::vector<uint8_t> pcm;
        {
            std::lock_guard<std::mutex> lock(self->queue_mutex_);
            if (self->audio_queue_.empty()) continue;
            pcm = std::move(self->audio_queue_.front());
            self->audio_queue_.pop_front();
        }
        self->FeedOne(std::move(pcm));
    }
}

void GeminiProtocol::FeedOne(std::vector<uint8_t> pcm)
{
    // The Application only accepts audio while in the Speaking state, which it reaches
    // a moment after our "tts start". Wait for it (bounded) instead of losing the opening.
    auto& app = Application::GetInstance();
    for (int i = 0; i < 200 && app.GetDeviceState() != kDeviceStateSpeaking; ++i) {
        if (!opened_ || !speaking_) return;  // interrupted or closed meanwhile
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (app.GetDeviceState() != kDeviceStateSpeaking || !on_incoming_audio_) return;
    if (on_output_level) on_output_level(rms_level(pcm));
    auto packet = std::make_unique<AudioStreamPacket>();
    packet->sample_rate = kGeminiOutputRate;
    packet->frame_duration = 0;  // raw PCM marker
    packet->payload = std::move(pcm);
    on_incoming_audio_(std::move(packet));
}

}  // namespace chan::platform
