#include "http_fetch.h"

#include <board.h>
#include <http.h>
#include <mooncake_log.h>
#include <network_interface.h>

namespace chan::platform {

namespace {
constexpr const char* kTag = "ChanFetch";
constexpr int kTimeoutMs = 12000;
}  // namespace

bool http_get_json(const std::string& url, JsonDocument& out, const JsonDocument* filter, std::string& error, size_t max_bytes)
{
    auto* network = Board::GetInstance().GetNetwork();
    if (!network) {
        error = "no network interface";
        return false;
    }
    auto http = network->CreateHttp(0);
    if (!http) {
        error = "could not create http client";
        return false;
    }
    http->SetTimeout(kTimeoutMs);
    http->SetHeader("User-Agent", "Chan/1.0 (+https://github.com/xavidop/chan)");
    http->SetHeader("Accept", "application/json");
    if (auto opened = http->Open("GET", url); !opened) {
        error = "open failed: " + opened.error().ToString();
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        error = "http status " + std::to_string(status ? *status : -1);
        http->Close();
        return false;
    }
    const size_t declared = http->GetBodyLength();
    if (declared > max_bytes) {
        error = "body too large: " + std::to_string(declared);
        http->Close();
        return false;
    }
    std::string body = http->ReadAll();
    http->Close();
    if (body.size() > max_bytes) {
        error = "body too large: " + std::to_string(body.size());
        return false;
    }
    DeserializationError err = filter ? deserializeJson(out, body, DeserializationOption::Filter(*filter))
                                      : deserializeJson(out, body);
    if (err) {
        error = std::string("json: ") + err.c_str();
        return false;
    }
    mclog::tagInfo(kTag, "GET ok: {} bytes from {}", body.size(), url.substr(0, 60));
    return true;
}

}  // namespace chan::platform
