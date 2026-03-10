#pragma once

#include "fastener/fastener.h"
#include "looker/file_system_model.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace looker {

class App {
public:
    int run();

private:
    bool initialize();
    bool loadDefaultFont();
    void configureMenus();
    void handleShortcuts();

    void renderFrame();
    void renderToolbar(float width, float y);
    void renderSidebar(const fst::Rect& bounds);
    void renderDirectoryTable(const fst::Rect& bounds);
    void renderDetailsPane(const fst::Rect& bounds);
    void renderStatusBar(float width, float height);
    void maybeShowContextMenu(const fst::Rect& bounds);

    void rebuildVisibleEntries();
    void rebuildNavigationModel();
    void rebuildFolderTree();

    void navigateTo(const std::filesystem::path& path);
    void navigateUp();
    void refreshCurrentDirectory();
    void setShowHiddenFiles(bool enabled);
    void selectEntry(int index);
    void openEntry(int index);

    const FileEntry* selectedEntry() const;
    bool openWithSystem(const std::filesystem::path& path);
    bool openContainingFolder(const std::filesystem::path& path);

    std::unique_ptr<fst::Window> window_;
    std::unique_ptr<fst::Context> ctx_;
    fst::MenuBar menuBar_;
    fst::TreeView folderTree_;
    fst::Table directoryTable_;
    FileSystemModel fileSystem_;

    std::vector<FileEntry> visibleEntries_;
    std::vector<std::string> breadcrumbItems_;
    std::vector<std::filesystem::path> breadcrumbPaths_;

    std::filesystem::path selectedPath_;
    std::string filterText_;
    std::string statusText_ = "Ready";

    float sidebarWidth_ = 280.0f;
    float tablePaneWidth_ = 760.0f;
    float toolbarHeight_ = 96.0f;
    float statusBarHeight_ = 28.0f;
    int selectedRow_ = -1;
    int sortColumn_ = 0;
    bool sortAscending_ = true;
    bool showDetailsPane_ = true;
    bool showHiddenFiles_ = false;
};

} // namespace looker
