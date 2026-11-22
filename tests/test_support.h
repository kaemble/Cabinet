#pragma once
#include <filesystem>
#include <cstdlib>
#include <stdexcept>

class TestDirectory {
public:
    std::filesystem::path path;
    explicit TestDirectory(const std::string& name, const std::filesystem::path& base = std::filesystem::temp_directory_path()) {
        std::filesystem::create_directories(base);
        auto pattern = (base / (name + "-XXXXXX")).string();
        const auto* result = mkdtemp(pattern.data());
        if (!result) throw std::runtime_error("Cannot create test directory");
        path = result;
    }
    ~TestDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
};
