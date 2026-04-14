# File Tinder

A file organization tool with three possible interfaces for quickly sorting and organizing files. For ADHD people who need to get things done but shutdown to a messy environment.
Made with a dream, Github Copilot, executive dysfunction and an obsessive attention to detail.


![File Tinder](https://img.shields.io/badge/Qt-6.x-green) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![License](https://img.shields.io/badge/License-MIT-yellow)

## Features

### Basic Mode
- **Swipe-style sorting**: Use arrow keys or buttons to quickly categorize files
  - → **Keep**: Keep file in original location
  - ← **Delete**: Mark file for deletion
  - ↓ **Skip**: Skip/ignore file
  - ↑ **Back**: Go back to previous file
- **Undo**: Revert your last action with Z
- **Image Preview**: Open images in a separate zoomable window with P
- **Filtering**: Filter by file type (Images, Videos, Audio, Documents, Archives, Other, Folders Only) or specify custom extensions
- **Sorting**: Order files by Name, Size, Type, or Date Modified (ascending/descending)
- **Progress tracking**: Visual progress bar and statistics
- **Session persistence**: Resume sorting sessions across application restarts

### Advanced Mode
- **Visual Mind Map**: Clickable folder nodes displayed as a scrollable tree
- **One-click file assignment**: Click any folder node to instantly move the current file there
- **Dynamic folder creation**: Create new or add existing folders on-the-fly via the "+" button
- **Quick Access bar**: Pin up to 10 frequently used folders for one-click access (keys 1-0)
- **Shared filtering & sorting**: Same filter/sort capabilities as Basic Mode
- **Keep, Delete, Skip, Undo**: Full file action controls below the mind map

### Review & Execute
- **Batch operations**: Review all decisions before execution
- **Summary view**: See file counts and sizes per destination
- **Safe deletion**: Files are moved to trash by default
- **Virtual folder creation**: Folders created in the mind map are auto-created during execution
- **Error handling**: Detailed error reporting for any issues

### Diagnostics
- **Built-in diagnostic tool**: Test database, file operations, MIME detection, screen info, and more
- **Application logging**: Full log viewer with export capability

## Requirements

- **CMake** 3.16 or higher
- **Qt 6.x** with the following modules:
  - Qt6::Core
  - Qt6::Widgets
  - Qt6::Gui
  - Qt6::Sql
- **C++17** compatible compiler

## Building

### Quick Build

```bash
# Clone the repository
git clone https://github.com/trabalhefabricio/file-tinder.git
cd file-tinder

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build . --parallel
```

### Detailed Build Instructions

#### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y cmake qt6-base-dev libqt6sql6-sqlite

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./FileTinder
```

#### Linux (Fedora)

```bash
# Install dependencies
sudo dnf install cmake qt6-qtbase-devel qt6-qttools-devel

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

#### macOS

```bash
# Install dependencies via Homebrew
brew install cmake qt@6

# Add Qt to PATH
export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"

# Build
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)

# Run
./FileTinder
```

#### Windows (Visual Studio)

```batch
REM Install Qt 6 from https://www.qt.io/download
REM Set Qt6_DIR environment variable

mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release) |
| `Qt6_DIR` | Auto-detected | Path to Qt6 CMake config |

## Usage

## Pristine Quality Checklist

Use the full release checklist here:

- [PRISTINE_QA_CHECKLIST.md](PRISTINE_QA_CHECKLIST.md)
  - Includes feature-by-feature checks plus AI and non-AI compatibility matrixes.

### Keyboard Shortcuts

#### Basic Mode
| Key | Action |
|-----|--------|
| → (Right Arrow) | Keep file |
| ← (Left Arrow) | Delete file |
| ↓ (Down Arrow) | Skip file |
| ↑ (Up Arrow) | Go back |
| Z | Undo last action |
| P | Open image preview window |
| Enter | Finish review |
| ? | Show shortcuts help |

#### Advanced Mode
| Key | Action |
|-----|--------|
| 1-9, 0 | Quick access folders (up to 10 slots) |
| ← or D | Delete file |
| ↓ or S | Skip file |
| ↑ | Go back |
| K | Keep file |
| Z | Undo last action |
| N | Add new folder to mind map |

### Workflow

1. **Select Folder**: Choose the folder containing files to organize
2. **Choose Mode**: Basic for simple sorting, Advanced for complex folder structures
3. **Sort Files**: Use swipes/clicks to assign each file to a destination
4. **Review**: Check the summary of all pending operations
5. **Execute**: Confirm and execute all moves/deletions

## Project Structure

```
file-tinder/
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── app/
│   ├── include/                # Header files
│   │   ├── ui_constants.hpp
│   │   ├── DatabaseManager.hpp
│   │   ├── FolderTreeModel.hpp
│   │   ├── FolderNodeWidget.hpp
│   │   ├── MindMapView.hpp
│   │   ├── FilterWidget.hpp
│   │   ├── ImagePreviewWindow.hpp
│   │   ├── FileTinderExecutor.hpp
│   │   ├── AppLogger.hpp
│   │   ├── DiagnosticTool.hpp
│   │   ├── StandaloneFileTinderDialog.hpp
│   │   └── AdvancedFileTinderDialog.hpp
│   ├── lib/                    # Source files
│   │   ├── main.cpp
│   │   ├── DatabaseManager.cpp
│   │   ├── FolderTreeModel.cpp
│   │   ├── FolderNodeWidget.cpp
│   │   ├── MindMapView.cpp
│   │   ├── FilterWidget.cpp
│   │   ├── ImagePreviewWindow.cpp
│   │   ├── FileTinderExecutor.cpp
│   │   ├── AppLogger.cpp
│   │   ├── DiagnosticTool.cpp
│   │   ├── StandaloneFileTinderDialog.cpp
│   │   └── AdvancedFileTinderDialog.cpp
│   └── resources/              # Resources
│       ├── resources.qrc
│       └── icons/
│           ├── folder.svg
│           ├── folder-new.svg
│           └── folder-linked.svg
└── proposal/                   # Original specification
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
    decision TEXT NOT NULL CHECK (decision IN ('pending', 'keep', 'delete', 'skip', 'move')),
    destination_folder TEXT,
    timestamp DATETIME,
    UNIQUE(folder_path, file_path)
);

-- Folder tree configuration (Advanced Mode mind map)
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

-- Folder connections (Advanced Mode grouped folders)
CREATE TABLE tinder_folder_connections (
    session_folder TEXT NOT NULL,
    group_id INTEGER NOT NULL,
    folder_path TEXT NOT NULL,
    UNIQUE(session_folder, folder_path)
);

-- Quick access folders (Advanced Mode, up to 10 slots)
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
- Icons from [Material Design Icons](https://materialdesignicons.com/)
