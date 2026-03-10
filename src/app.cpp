#include "looker/app.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
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

bool containsCaseInsensitive(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }

    return toLower(text).find(toLower(needle)) != std::string::npos;
}

std::shared_ptr<fst::TreeNode> makeDirectoryNode(const fs::path& path, const std::string& label) {
    return std::make_shared<fst::TreeNode>(path.string(), label, false);
}

std::string shellQuote(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool isHiddenPath(const fs::path& path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    const std::string name = path.filename().string();
    return !name.empty() && name.front() == '.';
#endif
}

} // namespace

int App::run() {
    if (!initialize()) {
        return 1;
    }

    while (window_->isOpen()) {
        window_->pollEvents();
        renderFrame();
    }

    return 0;
}

bool App::initialize() {
    fst::WindowConfig config;
    config.title = "Looker";
    config.width = 1440;
    config.height = 900;
    config.vsync = true;
    config.msaaSamples = 4;

    window_ = std::make_unique<fst::Window>(config);
    if (!window_ || !window_->isOpen()) {
        return false;
    }

    ctx_ = std::make_unique<fst::Context>();
    ctx_->setTheme(fst::Theme::dark());
    loadDefaultFont();
    fileSystem_.setShowHidden(showHiddenFiles_);

    std::vector<fst::TableColumn> columns = {
        {"name", "Name", 360.0f, 180.0f, 520.0f, fst::Alignment::Start, true, true},
        {"type", "Type", 130.0f, 80.0f, 180.0f, fst::Alignment::Start, true, true},
        {"size", "Size", 100.0f, 60.0f, 160.0f, fst::Alignment::End, true, true},
        {"modified", "Modified", 180.0f, 120.0f, 240.0f, fst::Alignment::Start, true, true}
    };
    directoryTable_.setColumns(columns);

    configureMenus();

    if (!fileSystem_.load(fs::current_path())) {
        statusText_ = fileSystem_.lastError().empty() ? "Failed to load the initial directory." : fileSystem_.lastError();
    } else {
        statusText_ = "Ready";
    }

    rebuildNavigationModel();
    rebuildVisibleEntries();
    return true;
}

bool App::loadDefaultFont() {
#ifdef _WIN32
    if (ctx_->loadFont("C:/Windows/Fonts/segoeui.ttf", 15.0f)) {
        return true;
    }
    return ctx_->loadFont("C:/Windows/Fonts/arial.ttf", 15.0f);
#else
    if (ctx_->loadFont("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 15.0f)) {
        return true;
    }
    return ctx_->loadFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f);
#endif
}

void App::configureMenus() {
    menuBar_.clear();

    menuBar_.addMenu("File", {
        fst::MenuItem("refresh", "Refresh", [this]() { refreshCurrentDirectory(); }).withShortcut("F5"),
        fst::MenuItem("up", "Go Up", [this]() { navigateUp(); }).withShortcut("Alt+Up"),
        fst::MenuItem::separator(),
        fst::MenuItem("exit", "Exit", [this]() {
            if (window_) {
                window_->close();
            }
        }).withShortcut("Alt+F4")
    });

    menuBar_.addMenu("View", {
        fst::MenuItem::checkbox("details", "Details Pane", &showDetailsPane_),
        fst::MenuItem::checkbox("show_hidden", "Show Hidden", &showHiddenFiles_, [this]() {
            setShowHiddenFiles(showHiddenFiles_);
        })
    });

    menuBar_.addMenu("Help", {
        fst::MenuItem("about", "About Looker", [this]() {
            statusText_ = "Looker explorer is running on Fastener.";
        })
    });
}

void App::handleShortcuts() {
    if (fst::IsContextMenuOpen(*ctx_) && ctx_->input().isKeyPressed(fst::Key::Escape)) {
        fst::CloseContextMenu(*ctx_);
        return;
    }

    if (ctx_->input().isKeyPressed(fst::Key::F5)) {
        refreshCurrentDirectory();
        return;
    }

    const fst::Modifiers modifiers = ctx_->input().modifiers();
    if (modifiers.alt && ctx_->input().isKeyPressed(fst::Key::Up)) {
        navigateUp();
        return;
    }

    if (modifiers.ctrl && ctx_->input().isKeyPressed(fst::Key::H)) {
        setShowHiddenFiles(!showHiddenFiles_);
        return;
    }

    if (!ctx_->isInputCaptured() && ctx_->input().isKeyPressed(fst::Key::Enter) && selectedRow_ >= 0) {
        openEntry(selectedRow_);
    }
}

void App::renderFrame() {
    ctx_->beginFrame(*window_);
    handleShortcuts();

    fst::DrawList& drawList = ctx_->drawList();
    const fst::Theme& theme = ctx_->theme();

    const float windowWidth = static_cast<float>(window_->width());
    const float windowHeight = static_cast<float>(window_->height());

    drawList.addRectFilled(
        fst::Rect(0.0f, 0.0f, windowWidth, windowHeight),
        theme.colors.windowBackground
    );

    const float menuHeight = menuBar_.render(*ctx_, fst::Rect(0.0f, 0.0f, windowWidth, 32.0f));
    renderToolbar(windowWidth, menuHeight);

    const float contentY = menuHeight + toolbarHeight_;
    const float contentHeight = windowHeight - contentY - statusBarHeight_;
    const fst::Rect contentBounds(0.0f, contentY, windowWidth, contentHeight);

    fst::SplitterOptions splitterOptions;
    splitterOptions.direction = fst::Direction::Vertical;
    splitterOptions.minSize1 = 220.0f;
    splitterOptions.minSize2 = 360.0f;
    splitterOptions.splitterWidth = 8.0f;
    fst::Splitter(*ctx_, "looker_main_splitter", sidebarWidth_, contentBounds, splitterOptions);

    const float splitterWidth = splitterOptions.splitterWidth;
    const fst::Rect sidebarBounds(contentBounds.x(), contentBounds.y(), sidebarWidth_, contentBounds.height());
    const fst::Rect mainBounds(
        contentBounds.x() + sidebarWidth_ + splitterWidth,
        contentBounds.y(),
        contentBounds.width() - sidebarWidth_ - splitterWidth,
        contentBounds.height()
    );

    renderSidebar(sidebarBounds);

    if (showDetailsPane_) {
        fst::SplitterOptions detailsSplitter;
        detailsSplitter.direction = fst::Direction::Vertical;
        detailsSplitter.minSize1 = 380.0f;
        detailsSplitter.minSize2 = 240.0f;
        detailsSplitter.splitterWidth = 8.0f;

        const float maxTableWidth = std::max(detailsSplitter.minSize1, mainBounds.width() - detailsSplitter.minSize2 - detailsSplitter.splitterWidth);
        tablePaneWidth_ = std::clamp(tablePaneWidth_, detailsSplitter.minSize1, maxTableWidth);
        fst::Splitter(*ctx_, "looker_details_splitter", tablePaneWidth_, mainBounds, detailsSplitter);

        const fst::Rect tableBounds(mainBounds.x(), mainBounds.y(), tablePaneWidth_, mainBounds.height());
        const fst::Rect detailsBounds(
            mainBounds.x() + tablePaneWidth_ + detailsSplitter.splitterWidth,
            mainBounds.y(),
            mainBounds.width() - tablePaneWidth_ - detailsSplitter.splitterWidth,
            mainBounds.height()
        );

        renderDirectoryTable(tableBounds);
        renderDetailsPane(detailsBounds);
    } else {
        renderDirectoryTable(mainBounds);
    }

    renderStatusBar(windowWidth, windowHeight);

    menuBar_.renderPopups(*ctx_);
    fst::RenderContextMenu(*ctx_);
    ctx_->endFrame();
    window_->swapBuffers();
}

void App::renderToolbar(const float width, const float y) {
    fst::PanelOptions panelOptions;
    panelOptions.title = "Navigation";
    panelOptions.style = fst::Style().withPos(0.0f, y).withSize(width, toolbarHeight_);
    panelOptions.spacing = 10.0f;

    Panel(*ctx_, "toolbar_panel", panelOptions) {
        fst::BeginHorizontal(*ctx_, 8.0f);

        fst::ButtonOptions smallButton;
        smallButton.style = fst::Style().withSize(90.0f, 30.0f);

        if (fst::Button(*ctx_, "Up", smallButton)) {
            navigateUp();
        }
        if (fst::Button(*ctx_, "Refresh", smallButton)) {
            refreshCurrentDirectory();
        }

        fst::TextInputOptions filterOptions;
        filterOptions.placeholder = "Filter current folder";
        filterOptions.style = fst::Style().withWidth(280.0f);
        if (fst::TextInput(*ctx_, "filter_input", filterText_, filterOptions)) {
            rebuildVisibleEntries();
        }

        fst::EndHorizontal(*ctx_);
        fst::Spacing(*ctx_, 8.0f);

        const int clickedIndex = fst::Breadcrumb(*ctx_, breadcrumbItems_);
        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(breadcrumbPaths_.size())) {
            navigateTo(breadcrumbPaths_[clickedIndex]);
        }

        fst::Spacing(*ctx_, 4.0f);
        fst::LabelSecondary(*ctx_, fileSystem_.currentPath().string());
    }
}

void App::renderSidebar(const fst::Rect& bounds) {
    fst::PanelOptions panelOptions;
    panelOptions.title = "Folders";
    panelOptions.style = fst::Style().withPos(bounds.x(), bounds.y()).withSize(bounds.width(), bounds.height());

    Panel(*ctx_, "folders_panel", panelOptions) {
        fst::TreeViewEvents events;
        events.onSelect = [this](fst::TreeNode* node) {
            if (node != nullptr) {
                navigateTo(fs::path(node->id));
            }
        };

        fst::TreeViewOptions options;
        options.rowHeight = 26.0f;
        options.showLines = false;

        const fst::Rect treeBounds = fst::AllocateRemainingSpace(*ctx_);
        folderTree_.render(*ctx_, "folder_tree", treeBounds, options, events);
    }
}

void App::renderDirectoryTable(const fst::Rect& bounds) {
    fst::PanelOptions panelOptions;
    panelOptions.title = "Contents";
    panelOptions.style = fst::Style().withPos(bounds.x(), bounds.y()).withSize(bounds.width(), bounds.height());

    Panel(*ctx_, "contents_panel", panelOptions) {
        fst::TableOptions options;
        options.alternateRowColors = true;
        options.scrollHeight = 0.0f;

        fst::TableEvents events;
        events.onSort = [this](const int columnIndex, const bool ascending) {
            sortColumn_ = columnIndex;
            sortAscending_ = ascending;
            rebuildVisibleEntries();
        };
        events.onRowClick = [this](const int rowIndex) {
            selectEntry(rowIndex);
        };
        events.onRowDoubleClick = [this](const int rowIndex) {
            openEntry(rowIndex);
        };

        directoryTable_.setSort(sortColumn_, sortAscending_);

        const fst::Rect tableBounds = fst::AllocateRemainingSpace(*ctx_);
        directoryTable_.begin(*ctx_, "directory_table", tableBounds, options, events);

        for (int index = 0; index < static_cast<int>(visibleEntries_.size()); ++index) {
            const FileEntry& entry = visibleEntries_[index];
            std::string displayName = entry.isDirectory ? "[DIR] " + entry.name : entry.name;
            if (entry.isHidden) {
                displayName = "[H] " + displayName;
            }

            directoryTable_.row(
                *ctx_,
                {displayName, entry.typeLabel, entry.sizeLabel, entry.modifiedLabel},
                index == selectedRow_
            );
        }

        directoryTable_.end(*ctx_);
    }

    maybeShowContextMenu(bounds);
}

void App::renderDetailsPane(const fst::Rect& bounds) {
    fst::PanelOptions panelOptions;
    panelOptions.title = "Details";
    panelOptions.style = fst::Style().withPos(bounds.x(), bounds.y()).withSize(bounds.width(), bounds.height());
    panelOptions.spacing = 8.0f;

    Panel(*ctx_, "details_panel", panelOptions) {
        const FileEntry* entry = selectedEntry();
        if (entry == nullptr) {
            fst::LabelHeading(*ctx_, "Current folder");
            fst::Label(*ctx_, fileSystem_.currentPath().filename().string().empty() ? fileSystem_.currentPath().string() : fileSystem_.currentPath().filename().string());
            fst::Spacing(*ctx_, 6.0f);
            fst::LabelSecondary(*ctx_, fileSystem_.currentPath().string());
            fst::Spacing(*ctx_, 10.0f);
            fst::Separator(*ctx_);
            fst::Spacing(*ctx_, 10.0f);
            fst::Label(*ctx_, "Select a file or folder to inspect it here.");
            fst::LabelSecondary(*ctx_, std::to_string(visibleEntries_.size()) + " visible items");
            fst::LabelSecondary(*ctx_, showHiddenFiles_ ? "Hidden files are visible" : "Hidden files are hidden");
            return;
        }

        fst::LabelHeading(*ctx_, entry->name);
        fst::LabelSecondary(*ctx_, entry->isDirectory ? "Folder" : "File");
        fst::Spacing(*ctx_, 10.0f);
        fst::Separator(*ctx_);
        fst::Spacing(*ctx_, 10.0f);

        fst::Label(*ctx_, "Path");
        fst::LabelSecondary(*ctx_, entry->path.string());
        fst::Spacing(*ctx_, 8.0f);

        fst::Label(*ctx_, "Type");
        fst::LabelSecondary(*ctx_, entry->typeLabel);
        fst::Spacing(*ctx_, 8.0f);

        fst::Label(*ctx_, "Size");
        fst::LabelSecondary(*ctx_, entry->sizeLabel);
        fst::Spacing(*ctx_, 8.0f);

        fst::Label(*ctx_, "Modified");
        fst::LabelSecondary(*ctx_, entry->modifiedLabel);
        fst::Spacing(*ctx_, 8.0f);

        fst::Label(*ctx_, "Hidden");
        fst::LabelSecondary(*ctx_, entry->isHidden ? "Yes" : "No");
        fst::Spacing(*ctx_, 12.0f);
        fst::Separator(*ctx_);
        fst::Spacing(*ctx_, 12.0f);

        fst::ButtonOptions buttonOptions;
        buttonOptions.style = fst::Style().withWidth(bounds.width() - 36.0f).withHeight(30.0f);

        if (fst::Button(*ctx_, entry->isDirectory ? "Open folder" : "Open", buttonOptions)) {
            openEntry(selectedRow_);
        }
        if (fst::Button(*ctx_, entry->isDirectory ? "Open in system explorer" : "Open containing folder", buttonOptions)) {
            if (openContainingFolder(entry->path)) {
                statusText_ = "Opened system explorer for " + entry->path.string();
            } else {
                statusText_ = "Unable to open the requested location in the system explorer.";
            }
        }
    }

    maybeShowContextMenu(bounds);
}

void App::renderStatusBar(const float width, const float height) {
    fst::StatusBarOptions options;
    options.height = statusBarHeight_;
    options.style = fst::Style().withPos(0.0f, height - statusBarHeight_).withSize(width, statusBarHeight_);

    if (fst::BeginStatusBar(*ctx_, options)) {
        fst::StatusBarSection(*ctx_, statusText_);
        fst::StatusBarSection(*ctx_, std::to_string(visibleEntries_.size()) + " items");
        fst::StatusBarSection(*ctx_, filterText_.empty() ? "Filter: none" : "Filter: " + filterText_);
        fst::StatusBarSection(*ctx_, showDetailsPane_ ? "Details: on" : "Details: off");
        fst::StatusBarSection(*ctx_, showHiddenFiles_ ? "Hidden: on" : "Hidden: off");
    }
    fst::EndStatusBar(*ctx_);
}

void App::maybeShowContextMenu(const fst::Rect& bounds) {
    if (!bounds.contains(ctx_->input().mousePos())) {
        return;
    }

    if (!ctx_->input().isMousePressed(fst::MouseButton::Right)) {
        return;
    }

    if (fst::IsMouseOverAnyMenu(*ctx_)) {
        return;
    }

    std::vector<fst::MenuItem> items;
    if (const FileEntry* entry = selectedEntry()) {
        items.emplace_back("open", entry->isDirectory ? "Open folder" : "Open", [this]() {
            openEntry(selectedRow_);
        });
        items.emplace_back("reveal", entry->isDirectory ? "Open in system explorer" : "Open containing folder", [this]() {
            const FileEntry* selected = selectedEntry();
            if (selected != nullptr && openContainingFolder(selected->path)) {
                statusText_ = "Opened system explorer for " + selected->path.string();
            } else {
                statusText_ = "Unable to open the requested location in the system explorer.";
            }
        });
        items.push_back(fst::MenuItem::separator());
    }

    items.emplace_back(
        fst::MenuItem::checkbox("toggle_hidden", "Show Hidden", &showHiddenFiles_, [this]() {
            setShowHiddenFiles(showHiddenFiles_);
        })
    );
    items.emplace_back("refresh", "Refresh", [this]() { refreshCurrentDirectory(); });
    items.emplace_back("up", "Go Up", [this]() { navigateUp(); });

    fst::ShowContextMenu(*ctx_, items, ctx_->input().mousePos());
    ctx_->input().consumeMouse();
}

void App::rebuildVisibleEntries() {
    visibleEntries_.clear();

    for (const FileEntry& entry : fileSystem_.entries()) {
        if (containsCaseInsensitive(entry.name, filterText_)) {
            visibleEntries_.push_back(entry);
        }
    }

    auto comparator = [this](const FileEntry& lhs, const FileEntry& rhs) {
        if (lhs.isDirectory != rhs.isDirectory) {
            return lhs.isDirectory && !rhs.isDirectory;
        }

        switch (sortColumn_) {
        case 1:
            if (lhs.typeLabel != rhs.typeLabel) {
                return sortAscending_ ? lhs.typeLabel < rhs.typeLabel : lhs.typeLabel > rhs.typeLabel;
            }
            break;
        case 2:
            if (lhs.sizeBytes != rhs.sizeBytes) {
                return sortAscending_ ? lhs.sizeBytes < rhs.sizeBytes : lhs.sizeBytes > rhs.sizeBytes;
            }
            break;
        case 3:
            if (lhs.modifiedTime != rhs.modifiedTime) {
                return sortAscending_ ? lhs.modifiedTime < rhs.modifiedTime : lhs.modifiedTime > rhs.modifiedTime;
            }
            break;
        default:
            break;
        }

        return sortAscending_ ? toLower(lhs.name) < toLower(rhs.name) : toLower(lhs.name) > toLower(rhs.name);
    };

    std::sort(visibleEntries_.begin(), visibleEntries_.end(), comparator);

    selectedRow_ = -1;
    if (!selectedPath_.empty()) {
        for (int index = 0; index < static_cast<int>(visibleEntries_.size()); ++index) {
            if (visibleEntries_[index].path == selectedPath_) {
                selectedRow_ = index;
                break;
            }
        }

        if (selectedRow_ < 0) {
            selectedPath_.clear();
        }
    }
}

void App::rebuildNavigationModel() {
    breadcrumbItems_.clear();
    breadcrumbPaths_.clear();
    rebuildFolderTree();

    const fs::path currentPath = fileSystem_.currentPath();
    if (currentPath.empty()) {
        return;
    }

    const fs::path rootPath = currentPath.root_path();
    if (!rootPath.empty()) {
        breadcrumbItems_.push_back(rootPath.string());
        breadcrumbPaths_.push_back(rootPath);

        fs::path accumulated = rootPath;
        const fs::path relativePath = currentPath.lexically_relative(rootPath);
        for (const fs::path& component : relativePath) {
            accumulated /= component;
            breadcrumbItems_.push_back(component.string());
            breadcrumbPaths_.push_back(accumulated);
        }
    } else {
        breadcrumbItems_.push_back(currentPath.string());
        breadcrumbPaths_.push_back(currentPath);
    }
}

void App::rebuildFolderTree() {
    const fs::path currentPath = fileSystem_.currentPath();
    if (currentPath.empty()) {
        return;
    }

    const fs::path rootPath = currentPath.root_path().empty() ? currentPath : currentPath.root_path();
    std::shared_ptr<fst::TreeNode> rootNode = makeDirectoryNode(rootPath, rootPath.string());
    rootNode->isExpanded = true;

    fst::TreeNode* currentNode = rootNode.get();
    fs::path accumulated = rootPath;

    const fs::path relativePath = currentPath.lexically_relative(rootPath);
    for (const fs::path& component : relativePath) {
        accumulated /= component;
        std::shared_ptr<fst::TreeNode> child = currentNode->addChild(accumulated.string(), component.string(), false);
        child->isExpanded = true;
        currentNode = child.get();
    }

    std::vector<fs::directory_entry> directories;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(currentPath, fs::directory_options::skip_permission_denied, error)) {
        if (error) {
            error.clear();
            break;
        }
        if (entry.is_directory(error)) {
            if (!showHiddenFiles_ && isHiddenPath(entry.path())) {
                error.clear();
                continue;
            }
            directories.push_back(entry);
        }
        error.clear();
    }

    std::sort(directories.begin(), directories.end(), [](const fs::directory_entry& lhs, const fs::directory_entry& rhs) {
        return toLower(lhs.path().filename().string()) < toLower(rhs.path().filename().string());
    });

    for (const fs::directory_entry& directory : directories) {
        currentNode->addChild(directory.path().string(), directory.path().filename().string(), false);
    }

    folderTree_.setRoot(rootNode);
    folderTree_.selectNode(currentNode);
}

void App::navigateTo(const fs::path& path) {
    if (fileSystem_.load(path)) {
        selectedRow_ = -1;
        selectedPath_.clear();
        statusText_ = "Opened " + fileSystem_.currentPath().string();
        rebuildNavigationModel();
        rebuildVisibleEntries();
        return;
    }

    statusText_ = fileSystem_.lastError().empty() ? "Unable to open the selected location." : fileSystem_.lastError();
}

void App::navigateUp() {
    if (fileSystem_.navigateUp()) {
        selectedRow_ = -1;
        selectedPath_.clear();
        statusText_ = "Moved to " + fileSystem_.currentPath().string();
        rebuildNavigationModel();
        rebuildVisibleEntries();
        return;
    }

    statusText_ = "Already at the top-level directory.";
}

void App::refreshCurrentDirectory() {
    fileSystem_.setShowHidden(showHiddenFiles_);
    if (fileSystem_.refresh()) {
        statusText_ = "Refreshed " + fileSystem_.currentPath().string();
        rebuildNavigationModel();
        rebuildVisibleEntries();
        return;
    }

    statusText_ = fileSystem_.lastError().empty() ? "Refresh failed." : fileSystem_.lastError();
}

void App::setShowHiddenFiles(const bool enabled) {
    showHiddenFiles_ = enabled;
    fileSystem_.setShowHidden(showHiddenFiles_);
    refreshCurrentDirectory();
    statusText_ = showHiddenFiles_ ? "Hidden files are now visible." : "Hidden files are now hidden.";
}

void App::selectEntry(const int index) {
    if (index < 0 || index >= static_cast<int>(visibleEntries_.size())) {
        selectedRow_ = -1;
        selectedPath_.clear();
        return;
    }

    selectedRow_ = index;
    selectedPath_ = visibleEntries_[index].path;
    statusText_ = selectedPath_.string();
}

void App::openEntry(const int index) {
    if (index < 0 || index >= static_cast<int>(visibleEntries_.size())) {
        return;
    }

    const FileEntry& entry = visibleEntries_[index];
    selectEntry(index);

    if (entry.isDirectory) {
        navigateTo(entry.path);
        return;
    }

    if (openWithSystem(entry.path)) {
        statusText_ = "Opened " + entry.path.string();
        return;
    }

    statusText_ = "Unable to open the selected file with the system handler.";
}

const FileEntry* App::selectedEntry() const {
    if (selectedRow_ >= 0 && selectedRow_ < static_cast<int>(visibleEntries_.size())) {
        return &visibleEntries_[selectedRow_];
    }

    if (selectedPath_.empty()) {
        return nullptr;
    }

    for (const FileEntry& entry : visibleEntries_) {
        if (entry.path == selectedPath_) {
            return &entry;
        }
    }

    return nullptr;
}

bool App::openWithSystem(const fs::path& path) {
#ifdef _WIN32
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#elif __APPLE__
    return std::system((std::string("open ") + shellQuote(path.string())).c_str()) == 0;
#else
    return std::system((std::string("xdg-open ") + shellQuote(path.string())).c_str()) == 0;
#endif
}

bool App::openContainingFolder(const fs::path& path) {
#ifdef _WIN32
    if (fs::is_directory(path)) {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(result) > 32;
    }

    const std::wstring parameters = L"/select,\"" + path.wstring() + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#elif __APPLE__
    const fs::path folder = fs::is_directory(path) ? path : path.parent_path();
    return std::system((std::string("open ") + shellQuote(folder.string())).c_str()) == 0;
#else
    const fs::path folder = fs::is_directory(path) ? path : path.parent_path();
    return std::system((std::string("xdg-open ") + shellQuote(folder.string())).c_str()) == 0;
#endif
}

} // namespace looker
