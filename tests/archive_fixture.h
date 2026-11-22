#pragma once
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
inline void write_zip(const std::filesystem::path& file, const std::vector<std::pair<std::string,std::string>>& files) {
    archive* out = archive_write_new(); archive_write_set_format_zip(out);
    if (archive_write_open_filename(out, file.c_str()) != ARCHIVE_OK) throw std::runtime_error("Cannot create ZIP fixture");
    for (const auto& [name, data] : files) {
        archive_entry* entry = archive_entry_new(); archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_size(entry, data.size()); archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_perm(entry, 0644);
        if (archive_write_header(out, entry) != ARCHIVE_OK || archive_write_data(out, data.data(), data.size()) != static_cast<la_ssize_t>(data.size())) throw std::runtime_error("Cannot write ZIP fixture");
        archive_entry_free(entry);
    }
    archive_write_close(out); archive_write_free(out);
}
