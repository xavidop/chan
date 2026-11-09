#include "http_fetch.h"

#include <board.h>
#include <esp_heap_caps.h>
#include <http.h>
#include <mooncake_log.h>
#include <network_interface.h>

namespace chan::platform {

namespace {
constexpr const char* kTag = "ChanFetch";
constexpr int kTimeoutMs = 12000;

struct PsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override
    {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t size) override
    {
        void* n = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return n ? n : realloc(p, size);
    }
};
}  // namespace

ArduinoJson::Allocator* psram_allocator()
{
    static PsramAllocator allocator;
    return &allocator;
}

bool http_get_json(const std::string& url, JsonDocument& out, const JsonDocument* filter, std::string& error, size_t max_bytes)
{
    auto* network = Board::GetInstance().GetNetwork();
    if (!network) {
        error = "no network interface";
        return false;
    }
    mclog::tagInfo(kTag, "GET {} (internal free {} B, largest DMA block {} B)", url.substr(0, 60),
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
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
    // Sports feeds nest deeper than ArduinoJson's default limit of 10.
    DeserializationError err = filter ? deserializeJson(out, body, DeserializationOption::Filter(*filter),
                                                        DeserializationOption::NestingLimit(32))
                                      : deserializeJson(out, body, DeserializationOption::NestingLimit(32));
    if (err) {
        error = std::string("json: ") + err.c_str();
        return false;
    }
    mclog::tagInfo(kTag, "GET ok: {} bytes from {} (free internal {} B)", body.size(), url.substr(0, 60),
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

}  // namespace chan::platform
