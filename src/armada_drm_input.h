#pragma once
#include <linux/input.h>
#include <algorithm>

namespace armada_drm
{
// Linux multitouch protocol B. DS/3DS expose one stylus, so use slot zero.
// Publish a contact only at SYN_REPORT, after its position has arrived.
struct TouchState
{
    int slot = 0, tracking = -1, raw_x = 0, raw_y = 0;
    input_absinfo axis_x{}, axis_y{};
    bool dropped = false;
    enum Result
    {
        None,
        Report,
        Reset
    };
    Result event(const input_event& e)
    {
        if (e.type == EV_SYN && e.code == SYN_DROPPED)
        {
            dropped = true;
            tracking = -1;
            return None;
        }
        if (dropped)
        {
            return (e.type == EV_SYN && e.code == SYN_REPORT) ? Reset : None;
        }
        if (e.type == EV_ABS)
        {
            if (e.code == ABS_MT_SLOT)
            {
                slot = e.value;
            }
            if (!slot)
            {
                if (e.code == ABS_MT_TRACKING_ID)
                {
                    tracking = e.value;
                }
                if (e.code == ABS_MT_POSITION_X)
                {
                    raw_x = e.value;
                }
                if (e.code == ABS_MT_POSITION_Y)
                {
                    raw_y = e.value;
                }
            }
        }
        return (e.type == EV_SYN && e.code == SYN_REPORT) ? Report : None;
    }
    bool down() const
    {
        return tracking >= 0 && !dropped;
    }
    float x() const
    {
        return normalized(raw_x, axis_x);
    }
    float y() const
    {
        return normalized(raw_y, axis_y);
    }

private:
    static float normalized(int value, const input_absinfo& axis)
    {
        return (axis.maximum > axis.minimum)
                   ? std::clamp(float(value - axis.minimum) / (axis.maximum - axis.minimum), 0.f, 1.f)
                   : 0;
    }
};
}
