#include "quiet_hours.h"

namespace chan {

bool in_quiet_hours(const QuietHours& q, int minute_of_day)
{
    if (!q.enabled) return false;
    if (q.start_min == q.end_min) return false;  // empty window
    if (q.start_min < q.end_min) {
        return minute_of_day >= q.start_min && minute_of_day < q.end_min;
    }
    // wraps midnight
    return minute_of_day >= q.start_min || minute_of_day < q.end_min;
}

}  // namespace chan
