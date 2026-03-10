#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace looker {

struct FileEntry {
    std::filesystem::path path;
    std::string name;
    std::string typeLabel;
    std::string sizeLabel;
    std::string modifiedLabel;
    std::uintmax_t sizeBytes = 0;
    std::time_t modifiedTime = 0;
    bool isDirectory = false;
    bool isHidden = false;
};

class FileSystemModel {
public:
    bool load(const std::filesystem::path& path);
    bool refresh();
    bool navigateUp();

    void setShowHidden(bool showHidden);
    bool showHidden() const;

    const std::filesystem::path& currentPath() const;
    const std::vector<FileEntry>& entries() const;
    const std::string& lastError() const;

private:
    static std::string classifyEntry(const std::filesystem::directory_entry& entry);
    static std::string formatFileSize(std::uintmax_t sizeBytes);
    static std::time_t toTimeT(std::filesystem::file_time_type value);
    static std::string formatModifiedTime(std::time_t modifiedTime);
    static bool isHiddenEntry(const std::filesystem::directory_entry& entry);

    std::filesystem::path currentPath_;
    std::vector<FileEntry> entries_;
    std::string lastError_;
    bool showHidden_ = false;
};

} // namespace looker
