#include "http_server.h"

#include <chan/core/agents.h>
#include <chan/platform/chan_app.h>
#include <application.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <mooncake_log.h>

#include <cstring>
#include <string>
#include <vector>

namespace chan::platform {

namespace {

constexpr const char* kTag = "ChanHttp";
httpd_handle_t g_server = nullptr;

// Compares in constant time over the longer length so timing does not leak the token.
bool token_matches(const std::string& provided, const std::string& expected)
{
    const size_t n = provided.size() > expected.size() ? provided.size() : expected.size();
    unsigned diff = provided.size() != expected.size();
    for (size_t i = 0; i < n; ++i) {
        const unsigned a = i < provided.size() ? static_cast<unsigned char>(provided[i]) : 0;
        const unsigned b = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0;
        diff |= a ^ b;
    }
    return diff == 0;
}

esp_err_t send_json(httpd_req_t* req, const char* status, const std::string& body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body.c_str(), body.size());
}

bool authorized(httpd_req_t* req)
{
    const size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0 || len > 256) return false;
    std::string header(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Authorization", header.data(), header.size()) != ESP_OK) return false;
    header.resize(len);
    const std::string prefix = "Bearer ";
    if (header.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string& expected = ChanApp::get().config().agent_token;
    return !expected.empty() && token_matches(header.substr(prefix.size()), expected);
}

esp_err_t health_handler(httpd_req_t* req)
{
    const auto& cfg = ChanApp::get().config();
    return send_json(req, "200 OK", std::string("{\"ok\":true,\"name\":\"") + cfg.name + "\"}");
}

esp_err_t agents_post_handler(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_json(req, "401 Unauthorized", R"({"error":"bad or missing bearer token"})");
    }
    if (req->content_len > kAgentEventMaxBytes) {
        return send_json(req, "413 Payload Too Large", R"({"error":"body over 4096 bytes"})");
    }
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        const int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n <= 0) return send_json(req, "400 Bad Request", R"({"error":"incomplete body"})");
        got += static_cast<size_t>(n);
    }
    AgentEvent event;
    std::string error;
    if (!parse_agent_event(body, event, error)) {
        return send_json(req, "400 Bad Request", std::string("{\"error\":\"") + error + "\"}");
    }
    ChanApp::get().onAgentEvent(event);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t agents_get_handler(httpd_req_t* req)
{
    if (!authorized(req)) return send_json(req, "401 Unauthorized", R"({"error":"bad or missing bearer token"})");
    return send_json(req, "200 OK", ChanApp::get().agentStatusJson());
}

void reboot_timer_cb(void*)
{
    esp_restart();
}

esp_err_t ota_post_handler(httpd_req_t* req)
{
    if (!authorized(req)) return send_json(req, "401 Unauthorized", R"({"error":"bad or missing bearer token"})");
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        return send_json(req, "409 Conflict", R"({"error":"busy: a voice session is active, try again in a moment"})");
    }
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) return send_json(req, "500 Internal Server Error", R"({"error":"no OTA partition"})");
    if (req->content_len == 0 || req->content_len > part->size) {
        return send_json(req, "413 Payload Too Large", R"({"error":"image size does not fit the OTA partition"})");
    }
    mclog::tagInfo(kTag, "OTA: receiving {} bytes into {}", req->content_len, part->label);
    // Quiet the radio's other users for the duration: no feed fetching, no modem sleep.
    ChanApp::get().setOtaInProgress(true);
    esp_wifi_set_ps(WIFI_PS_NONE);
    struct OtaScope {
        ~OtaScope()
        {
            ChanApp::get().setOtaInProgress(false);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
    } scope;
    // If we are still "pending verify" from a previous update, the fact that we are up, on Wi-Fi
    // and serving this request is proof enough: confirm it, otherwise esp_ota_begin refuses.
    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            mclog::tagInfo(kTag, "OTA: confirmed the running image before updating");
        }
    }
    esp_ota_handle_t handle = 0;
    if (esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle); err != ESP_OK) {
        return send_json(req, "500 Internal Server Error",
                         std::string(R"({"error":"esp_ota_begin failed: )") + esp_err_to_name(err) + "\"}");
    }
    uint32_t last_log = 0;
    std::vector<char> buf(4096);
    size_t received = 0;
    bool first = true;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, buf.data(), buf.size());
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;  // slow link: keep waiting
        if (n <= 0) {
            esp_ota_abort(handle);
            mclog::tagWarn(kTag, "OTA: upload interrupted after {} bytes (recv={})", received, n);
            return send_json(req, "400 Bad Request", R"({"error":"upload interrupted"})");
        }
        if (received - last_log >= 512 * 1024) {
            last_log = received;
            mclog::tagInfo(kTag, "OTA: {} / {} bytes", received, req->content_len);
        }
        if (first) {
            first = false;
            if (static_cast<uint8_t>(buf[0]) != 0xE9) {  // ESP image magic
                esp_ota_abort(handle);
                return send_json(req, "400 Bad Request", R"({"error":"not an ESP32 firmware image"})");
            }
        }
        if (esp_ota_write(handle, buf.data(), static_cast<size_t>(n)) != ESP_OK) {
            esp_ota_abort(handle);
            return send_json(req, "500 Internal Server Error", R"({"error":"flash write failed"})");
        }
        received += static_cast<size_t>(n);
    }
    const esp_err_t end = esp_ota_end(handle);
    if (end != ESP_OK) {
        return send_json(req, "400 Bad Request", std::string(R"({"error":"image rejected: )") + esp_err_to_name(end) + "\"}");
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        return send_json(req, "500 Internal Server Error", R"({"error":"could not set boot partition"})");
    }
    mclog::tagInfo(kTag, "OTA: image accepted, rebooting");
    static esp_timer_handle_t reboot_timer = nullptr;
    if (!reboot_timer) {
        const esp_timer_create_args_t args = {.callback = reboot_timer_cb, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                              .name = "chan_reboot", .skip_unhandled_events = true};
        esp_timer_create(&args, &reboot_timer);
    }
    esp_timer_start_once(reboot_timer, 1500 * 1000);
    return send_json(req, "200 OK", std::string(R"({"ok":true,"bytes":)") + std::to_string(received) + R"(,"partition":")" + part->label + "\"}");
}

void start_mdns(const ChanConfig& cfg)
{
    if (mdns_init() != ESP_OK) {
        mclog::tagWarn(kTag, "mdns init failed");
        return;
    }
    mdns_hostname_set(cfg.hostname.c_str());
    mdns_instance_name_set(cfg.name.c_str());
    mdns_service_add(nullptr, "_chan", "_tcp", 80, nullptr, 0);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    mclog::tagInfo(kTag, "mdns: {}.local", cfg.hostname);
}

}  // namespace

bool http_server_running()
{
    return g_server != nullptr;
}

void http_server_start(const ChanConfig& cfg)
{
    if (g_server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 20;  // seconds; OTA chunks can stall while flash erases
    if (httpd_start(&g_server, &config) != ESP_OK) {
        mclog::tagError(kTag, "httpd start failed");
        g_server = nullptr;
        return;
    }
    static const httpd_uri_t health = {.uri = "/api/health", .method = HTTP_GET, .handler = health_handler, .user_ctx = nullptr};
    static const httpd_uri_t agents_post = {.uri = "/api/agents", .method = HTTP_POST, .handler = agents_post_handler, .user_ctx = nullptr};
    static const httpd_uri_t agents_get = {.uri = "/api/agents", .method = HTTP_GET, .handler = agents_get_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(g_server, &health);
    httpd_register_uri_handler(g_server, &agents_post);
    httpd_register_uri_handler(g_server, &agents_get);
    static const httpd_uri_t ota_post = {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(g_server, &ota_post);
    start_mdns(cfg);
    mclog::tagInfo(kTag, "listening on http://{}.local/", cfg.hostname);
}

}  // namespace chan::platform
