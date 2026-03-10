#include "looker/file_system_model.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace looker {

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

bool FileSystemModel::load(const fs::path& path) {
    std::error_code error;
    fs::path absolutePath = path.empty() ? fs::current_path(error) : fs::absolute(path, error);
    if (error) {
        lastError_ = "Unable to resolve path.";
        return false;
    }

    if (!fs::exists(absolutePath, error) || error) {
        lastError_ = "Selected path does not exist.";
        return false;
    }

    if (!fs::is_directory(absolutePath, error) || error) {
        lastError_ = "Selected path is not a directory.";
        return false;
    }

    std::vector<FileEntry> freshEntries;
    fs::directory_iterator iterator(
        absolutePath,
        fs::directory_options::skip_permission_denied,
        error
    );

    if (error) {
        lastError_ = "Access denied while reading directory.";
        return false;
    }

    for (const fs::directory_entry& entry : iterator) {
        FileEntry fileEntry;
        fileEntry.isHidden = isHiddenEntry(entry);
        if (!showHidden_ && fileEntry.isHidden) {
            continue;
        }

        fileEntry.path = entry.path();
        fileEntry.name = entry.path().filename().string();
        fileEntry.isDirectory = entry.is_directory(error);
        if (error) {
            error.clear();
            fileEntry.isDirectory = false;
        }

        if (fileEntry.name.empty()) {
            fileEntry.name = entry.path().string();
        }

        fileEntry.typeLabel = classifyEntry(entry);

        if (!fileEntry.isDirectory) {
            fileEntry.sizeBytes = entry.file_size(error);
            if (error) {
                error.clear();
                fileEntry.sizeBytes = 0;
                fileEntry.sizeLabel = "--";
            } else {
                fileEntry.sizeLabel = formatFileSize(fileEntry.sizeBytes);
            }
        } else {
            fileEntry.sizeLabel = "--";
        }

        const fs::file_time_type writeTime = entry.last_write_time(error);
        if (error) {
            error.clear();
            fileEntry.modifiedTime = 0;
            fileEntry.modifiedLabel = "--";
        } else {
            fileEntry.modifiedTime = toTimeT(writeTime);
            fileEntry.modifiedLabel = formatModifiedTime(fileEntry.modifiedTime);
        }

        freshEntries.push_back(fileEntry);
    }

    std::sort(freshEntries.begin(), freshEntries.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        if (lhs.isDirectory != rhs.isDirectory) {
            return lhs.isDirectory && !rhs.isDirectory;
        }
        return toLower(lhs.name) < toLower(rhs.name);
    });

    currentPath_ = absolutePath;
    entries_ = std::move(freshEntries);
    lastError_.clear();
    return true;
}

bool FileSystemModel::refresh() {
    return load(currentPath_);
}

bool FileSystemModel::navigateUp() {
    if (currentPath_.empty()) {
        return false;
    }

    const fs::path parent = currentPath_.parent_path();
    if (parent.empty() || parent == currentPath_) {
        return false;
    }

    return load(parent);
}

void FileSystemModel::setShowHidden(const bool showHidden) {
    showHidden_ = showHidden;
}

bool FileSystemModel::showHidden() const {
    return showHidden_;
}

const fs::path& FileSystemModel::currentPath() const {
    return currentPath_;
}

const std::vector<FileEntry>& FileSystemModel::entries() const {
    return entries_;
}

const std::string& FileSystemModel::lastError() const {
    return lastError_;
}

std::string FileSystemModel::classifyEntry(const fs::directory_entry& entry) {
    std::error_code error;
    if (entry.is_directory(error)) {
        return "Folder";
    }

    const std::string extension = entry.path().extension().string();
    if (extension.empty()) {
        return "File";
    }

    std::string label = extension;
    if (!label.empty() && label.front() == '.') {
        label.erase(label.begin());
    }

    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    return label + " file";
}

std::string FileSystemModel::formatFileSize(const std::uintmax_t sizeBytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(sizeBytes);
    int unitIndex = 0;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        ++unitIndex;
    }

    std::ostringstream stream;
    if (unitIndex == 0) {
        stream << static_cast<std::uintmax_t>(size);
    } else {
        stream << std::fixed << std::setprecision(1) << size;
    }
    stream << ' ' << units[unitIndex];
    return stream.str();
}

std::time_t FileSystemModel::toTimeT(const fs::file_time_type value) {
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(systemTime);
}

std::string FileSystemModel::formatModifiedTime(const std::time_t modifiedTime) {
    if (modifiedTime == 0) {
        return "--";
    }

    std::tm timeInfo {};
#ifdef _WIN32
    localtime_s(&timeInfo, &modifiedTime);
#else
    localtime_r(&modifiedTime, &timeInfo);
#endif

    std::ostringstream stream;
    stream << std::put_time(&timeInfo, "%Y-%m-%d %H:%M");
    return stream.str();
}

bool FileSystemModel::isHiddenEntry(const fs::directory_entry& entry) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(entry.path().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    const std::string name = entry.path().filename().string();
    return !name.empty() && name.front() == '.';
#endif
}

} // namespace looker
