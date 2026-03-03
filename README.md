# File Tinder

A file organization tool with three sorting modes (Basic, Advanced, AI) for quickly sorting and organizing files. Designed for people who need to get things done but shut down in messy environments.

Made with a dream, GitHub Copilot, executive dysfunction and an obsessive attention to detail.

![File Tinder](https://img.shields.io/badge/Qt-6.x-green) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![License](https://img.shields.io/badge/License-MIT-yellow)

## Why File Tinder?

Organizing files is tedious. You open a folder with hundreds of files and immediately feel overwhelmed. File Tinder turns it into a simple, one-file-at-a-time decision process — like swiping on Tinder, but for your files. Three modes let you choose how deep you want to go:

- **Basic Mode**: Swipe right to keep, left to delete, down to sort later. That's it.
- **Advanced Mode**: A visual grid of destination folders. Click one to move the file there. Create folders on the fly.
- **AI Mode**: Connect to any OpenAI-compatible API and let AI suggest where your files should go. Review the suggestions or let it run fully automatic.

All three modes share the same session state, so you can switch between them freely.

## Features

### Basic Mode
- **Swipe-style sorting**: Arrow keys or buttons to quickly categorize files
  - → **Keep**: Keep file in original location
  - ← **Delete**: Mark file for deletion
  - ↓ **Sort Later**: Move file to end of list (see it again later)
  - **Z/Y**: Undo/Redo last action
- **Image preview**: Opens images in a separate zoomable window (P key)
- **File type filtering**: Images, Videos, Audio, Documents, Archives, Other, Folders Only, or custom extensions
- **Subfolder depth control**: Spinner (0–5) to include files from subfolders
- **Multi-field sorting**: By Name, Size, Type, or Date Modified (ascending/descending)
- **Progress tracking**: Visual progress bar showing files sorted vs. remaining
- **Session persistence**: Resume sorting sessions across application restarts
- **Duplicate detection**: Automatic SHA-256 hash detection with "Keep Newest" bulk action
- **Search**: Ctrl+F to search files by name

### Advanced Mode
- **Visual folder grid**: Clickable folder buttons in a scrollable grid layout
- **One-click file assignment**: Click any folder to move the current file there
- **Dynamic folder management**: Create new or add existing folders via [+], right-click for rename/color/delete
- **Manual Edit dialog**: Batch editor for grid folders — tree/text toggle, middle-click delete, add folders/subfolders
- **Quick Access bar**: Pin up to 10 folders for one-click access (keys 1-0)
- **Grid customization**: Height spinner, auto width, 3-level path display (Off/Relative/Full), adjustable button width, row count control, visual hierarchy toggle
- **Grid configurations**: Save/load named grid layouts for reuse across sessions
- **Folder coloring**: 8 preset colors via right-click context menu
- **Keyboard navigation**: Tab enters grid navigation, arrow keys move between folders, Space to assign
- **Sort dropdown**: Manual / A-Z / By Count ordering for grid folders
- **Expand grid**: Fullscreen toggle for the folder grid
- **Node swapping**: Drag folders to reorder within the grid

### AI Mode
- **Any OpenAI-compatible API**: Works with OpenAI, Anthropic, Google, Groq, local models (LM Studio, Ollama)
- **Two sorting modes**:
  - **Auto**: AI assigns every file to a folder automatically
  - **Semi-Auto**: AI highlights top matching folders per file, you make the final click
- **Category generation**: AI proposes folder structure based on your file collection. You review/edit categories before creation.
- **AI Reasoning display**: Toggle to see why the AI chose each folder (reasoning text per file)
- **Cost estimation**: Real-time token cost estimates with provider-specific pricing
- **Budget control**: Set a maximum budget ($0.00 = no limit)
- **Smart rate limiting**: Automatic per-provider pacing with 429-retry and exponential backoff
- **AI glow borders**: Gold glow on grid nodes that AI suggests for the current file (toggle)
- **Folder depth control**: Choose how many levels deep AI should create subfolders (1-3)

### File List Window
- **Floating tool window**: Opens via F key or button. Always shows the full file list.
- **Multi-select**: Extended selection with right-click to assign files to folders or set decisions (Keep/Delete/Sort Later)
- **Sorting**: By Name, Decision status, or Extension
- **Real-time status**: Shows decision status ([K]/[D]/[L]/[M]) and file sizes per item
- **Cross-mode sync**: Changes in the file list propagate to all modes instantly

### Review & Execute Screen
- **Full file table**: Shows ALL files (including pending) with color-coded decisions
- **Columns**: File name, Size, Decision, Destination, Mode (move/copy)
- **Inline editing**: Change decisions and destinations directly in the table
- **Filter & sort**: Filter by decision type, sort by name/decision/destination
- **Bulk actions**: "All Keep", "All Delete", "All Sort Later", "All Pending" buttons
- **Preview on hover**: Toggle to see file path, size, MIME type when hovering rows
- **Folder picker**: Browse button beside each destination for selecting folders outside the grid
- **Destination disambiguation**: When folders share names, shows parent/name to distinguish
- **Copy mode**: Change any row to "copy" to duplicate rather than move
- **Consolidate button**: Finalizes all changes. Undone operations become permanent after this.
- **Export log**: Save execution results as CSV

### Execution Results
- **Session statistics**: Files reviewed, kept, deleted, sorted later, moved. Review pace (files/minute).
- **Disk space freed**: Shows total bytes recovered from deletions
- **Execution log**: Table of all operations with per-row undo buttons
- **Filter by action**: Show only Moved, Deleted, or Kept entries
- **Undo All**: Batch-undo all reversible operations
- **Export**: Save the full log as CSV

### Duplicate Detection
- **SHA-256 hashing**: Finds exact duplicate files by content hash
- **Group view**: Tree widget showing duplicate groups with file details
- **Keep Newest**: One-click to mark all duplicates except the most recent for deletion
- **Column sorting**: Sort by any column in the duplicate tree

### Launcher / Main Screen
- **Mode selection**: Basic, Advanced, or AI mode
- **Multiple folders**: Toggle to add multiple source folders to a single session
- **Recent folders**: Quick access to previously used source folders (middle-click to remove)
- **Dark/Light theme**: Toggle between dark and light color schemes
- **User Data**: Activity log of all past executions; AI-powered personality analysis and organization tips
- **Clear session**: Reset all decisions and start fresh
- **Session resume**: Shows "Session in progress" label when a previous session has sorted files

## Requirements

- **CMake** 3.16 or higher
- **Qt 6.x** with the following modules:
  - Qt6::Core
  - Qt6::Widgets
  - Qt6::Gui
  - Qt6::Sql
  - Qt6::Network (for AI Mode)
- **C++17** compatible compiler

## Building

### Quick Build

```bash
git clone https://github.com/trabalhefabricio/file-tinder.git
cd file-tinder
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y cmake qt6-base-dev libqt6sql6-sqlite libqt6network6

mkdir build && cd build
cmake ..
make -j$(nproc)
./FileTinder
```

### Linux (Fedora)

```bash
sudo dnf install cmake qt6-qtbase-devel qt6-qttools-devel
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### macOS

```bash
brew install cmake qt@6
export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
./FileTinder
```

### Windows (Visual Studio)

```batch
REM Install Qt 6 from https://www.qt.io/download
REM Set Qt6_DIR environment variable
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

## Keyboard Shortcuts

### All Modes
| Key | Action |
|-----|--------|
| → (Right Arrow) | Keep file |
| ← (Left Arrow) | Delete file |
| ↓ (Down Arrow) | Sort later (move to end of list) |
| Z or Backspace | Undo last action |
| Y | Redo last undone action |
| P | Open image preview window |
| F | Open File List window |
| Ctrl+F | Focus search box |
| Enter | Finish review |
| ? or / | Show shortcuts help |
| Esc | Close dialog |

### Advanced / AI Mode
| Key | Action |
|-----|--------|
| 1-9, 0 | Quick access folders (up to 10 slots) |
| K | Keep file |
| D | Delete file |
| S | Sort later |
| N | Add new folder to grid |
| Tab | Enter grid keyboard navigation |
| Arrow keys (in grid mode) | Navigate between folder buttons |
| Space (in grid mode) | Assign file to focused folder |
| Esc (in grid mode) | Exit grid navigation |

## Project Structure

```
file-tinder/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── LICENSE
├── app/
│   ├── include/
│   │   ├── ui_constants.hpp         # UI scaling, colors, dimensions
│   │   ├── DatabaseManager.hpp      # SQLite session persistence
│   │   ├── FolderTreeModel.hpp      # Folder node tree data model
│   │   ├── MindMapView.hpp          # Visual folder grid widget
│   │   ├── FilterWidget.hpp         # File type filter bar
│   │   ├── ImagePreviewWindow.hpp   # Zoomable image preview
│   │   ├── FileListWindow.hpp       # Floating file list tool window
│   │   ├── FileTinderExecutor.hpp   # Move/delete/copy execution engine
│   │   ├── DuplicateDetectionWindow.hpp  # SHA-256 duplicate finder
│   │   ├── AppLogger.hpp            # Application logging
│   │   ├── UserDataDialog.hpp       # User data and activity log
│   │   ├── ManualEditDialog.hpp     # Grid folder batch editor
│   │   ├── StandaloneFileTinderDialog.hpp  # Basic mode dialog
│   │   ├── AdvancedFileTinderDialog.hpp    # Advanced mode dialog
│   │   └── AiFileTinderDialog.hpp          # AI mode dialog
│   ├── lib/                         # Source implementations
│   └── resources/
│       ├── resources.qrc
│       └── icons/
```

## Database

File Tinder uses SQLite for session persistence. The database is stored in:
- **Linux**: `~/.local/share/FileTinder/file_tinder.db`
- **macOS**: `~/Library/Application Support/FileTinder/file_tinder.db`
- **Windows**: `%APPDATA%/FileTinder/file_tinder.db`

### Schema

```sql
-- File decisions (session state)
CREATE TABLE file_tinder_state (
    folder_path TEXT NOT NULL,
    file_path TEXT NOT NULL,
    decision TEXT NOT NULL,
    destination_folder TEXT,
    decided_in_mode TEXT,
    timestamp DATETIME,
    UNIQUE(folder_path, file_path)
);

-- Folder tree configuration
CREATE TABLE tinder_folder_tree (
    session_folder TEXT NOT NULL,
    folder_path TEXT NOT NULL,
    display_name TEXT,
    is_virtual INTEGER DEFAULT 0,
    is_pinned INTEGER DEFAULT 0,
    parent_path TEXT,
    sort_order INTEGER DEFAULT 0,
    UNIQUE(session_folder, folder_path)
);

-- Quick access folders (up to 10 slots)
CREATE TABLE quick_access_folders (
    session_folder TEXT NOT NULL,
    folder_path TEXT NOT NULL,
    slot_order INTEGER NOT NULL,
    UNIQUE(session_folder, slot_order)
);

-- Recent folders (launcher history)
CREATE TABLE recent_folders (
    folder_path TEXT NOT NULL UNIQUE,
    timestamp DATETIME
);

-- AI provider settings
CREATE TABLE ai_providers (
    session_folder TEXT NOT NULL,
    provider TEXT,
    endpoint TEXT,
    api_key TEXT,
    model TEXT,
    UNIQUE(session_folder)
);

-- Grid configurations (named layouts)
CREATE TABLE grid_configs (
    session_folder TEXT NOT NULL,
    config_name TEXT NOT NULL,
    paths TEXT,
    UNIQUE(session_folder, config_name)
);

-- Execution log (for undo support)
CREATE TABLE execution_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_folder TEXT NOT NULL,
    action TEXT NOT NULL,
    source_path TEXT NOT NULL,
    dest_path TEXT,
    timestamp DATETIME
);

-- Session source folders (multiple folder support)
CREATE TABLE session_source_folders (
    session_folder TEXT NOT NULL,
    source_path TEXT NOT NULL,
    sort_order INTEGER DEFAULT 0,
    UNIQUE(session_folder, source_path)
);
```

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by the Tinder swipe interface
- Built with [Qt 6](https://www.qt.io/)
- AI integration via OpenAI-compatible APIs
