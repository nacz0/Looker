# Looker

Looker is a desktop file explorer built with C++17 and Fastener.

## Current Scope

- standalone repository with its own CMake build
- local Fastener integration through `add_subdirectory`
- filesystem-backed directory listing via `std::filesystem`
- first MVP screen with breadcrumbs, folder tree, file table, filtering, and status bar

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

If `Fastener` lives somewhere else, point CMake at it:

```powershell
cmake -S . -B build -DLOOKER_FASTENER_DIR=C:\path\to\Fastener
```

## Run

```powershell
.\build\Release\looker.exe
```

## Development

Parts of this project were implemented using an agentic coding workflow with Codex.
AI assistance was used to accelerate development, while architecture, design
decisions, debugging and integration were performed manually.

## Next Iterations

- richer directory tree
- opening files with system handlers
- context menus and keyboard shortcuts
- details/preview pane

