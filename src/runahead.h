#pragma once
#include "save_manager.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

bool deterministic_core(const std::filesystem::path& core);

class RunAhead
{
public:
    bool state_operation = false;
    bool failed = false;
    std::string error;
    void run(const SaveCore& core, unsigned frames,
             const std::function<void(bool output, bool speculative)>& execute);

private:
    std::vector<char> state;
    unsigned requested_frames = 0;
};
