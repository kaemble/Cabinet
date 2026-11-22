#include "runahead.h"
#include "core_metadata.h"
#include <algorithm>

bool deterministic_core(const std::filesystem::path& core)
{
    const auto name = core.stem().string();
    for (const auto known : deterministic_cores)
    {
        if (name == std::string(known) + "_libretro")
        {
            return true;
        }
    }
    return false;
}

void RunAhead::run(const SaveCore& core, unsigned frames,
                  const std::function<void(bool, bool)>& execute)
{
    if (frames != requested_frames)
    {
        failed = false;
        error.clear();
        requested_frames = frames;
    }
    if (!frames || failed)
    {
        execute(true, false);
        return;
    }
    struct StateContext
    {
        bool& active;
        explicit StateContext(bool& active) : active(active)
        {
            active = true;
        }
        ~StateContext()
        {
            active = false;
        }
    };
    std::size_t size = state.size();
    if (state.empty())
    {
        StateContext context(state_operation);
        size = core.serialize_size();
    }
    if (!size)
    {
        // Some cores need ordinary startup frames before states are available.
        execute(true, false);
        return;
    }
    if (size > SaveManager::max_state_size)
    {
        failed = true;
        error = "Run-ahead disabled: save state exceeds the memory limit";
        execute(true, false);
        return;
    }
    state.resize(size);
    execute(false, false);
    {
        StateContext context(state_operation);
        size = core.serialize_size();
        if (!size || size > SaveManager::max_state_size)
        {
            failed = true;
            error = "Run-ahead disabled: core changed its save-state size";
            return;
        }
        state.resize(size);
        if (!core.serialize(state.data(), state.size()))
        {
            failed = true;
            error = "Run-ahead disabled: core could not save state";
            return;
        }
    }
    try
    {
        for (unsigned frame = 0; frame < std::min(frames, 3u); ++frame)
        {
            execute(frame + 1 == std::min(frames, 3u), true);
        }
    }
    catch (...)
    {
        StateContext context(state_operation);
        if (!core.unserialize(state.data(), state.size()))
        {
            throw StateRecoveryError("Run-ahead could not restore the retained state");
        }
        throw;
    }
    StateContext context(state_operation);
    if (!core.unserialize(state.data(), state.size()))
    {
        failed = true;
        throw StateRecoveryError("Run-ahead could not restore the retained state");
    }
}
