// Tracks whether the periodic internet fetches (location, weather, scores) keep failing while
// Wi-Fi still says it is associated: a router with no uplink looks "online" to the station.
// Pure C++17, host-tested. Written from the network task, read from the UI task via
// std::atomic, so it carries no locks of its own.
#pragma once

#include <atomic>

namespace chan {

class FetchHealth {
public:
    static constexpr int kDefaultThreshold = 3;

    explicit FetchHealth(int threshold = kDefaultThreshold) : _threshold(threshold < 1 ? 1 : threshold) {}

    // One fetch finished: ok = the HTTP request itself succeeded (parse errors are not outages).
    void record(bool ok);
    // True once `threshold` fetches in a row failed; cleared by the next success.
    bool down() const { return _failures.load() >= _threshold; }
    int consecutive_failures() const { return _failures.load(); }

private:
    int _threshold;
    std::atomic<int> _failures{0};
};

}  // namespace chan
