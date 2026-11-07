#include "net_health.h"

namespace chan {

void FetchHealth::record(bool ok)
{
    if (ok) {
        _failures.store(0);
        return;
    }
    int n = _failures.load();
    if (n < 1000000) _failures.store(n + 1);  // the count only matters up to the threshold
}

}  // namespace chan
