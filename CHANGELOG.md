# Changelog

All notable changes to File Tinder are documented in this file.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] — 2026-02-10

Initial release of File Tinder — a desktop file organizer built with Qt 6 and C++.

### Added
- **Basic Mode**: Swipe-style file sorting with arrow keys (→ Keep, ← Delete, ↓ Skip, ↑ Back)
- **Advanced Mode**: Visual mind map / folder grid for organizing files into destination folders
- **FilterWidget**: 8 filter types (All Files, Images, Videos, Audio, Documents, Archives, Code, Text) plus custom extension filter and 4 sort fields
- **Session persistence** via SQLite database — resume sorting across restarts
- **Image preview** with P key toggle
- **Undo system** via Z / Backspace
- **CMake build system** with Qt 6 (Core, Widgets, Gui, Sql modules)

---

## [0.2.0] — 2026-02-10

### Added
- **DiagnosticTool**: Screen Information test, Qt/System Version test
- `closeEvent` now prompts Save / Discard / Cancel before closing

### Changed
- Window sizing reduced — Basic: 700×550, Advanced: 800×600
- Debounced resize handler via QTimer

### Fixed
- **Crash fix**: Animation memory safety using QPointer
- Null checks throughout AdvancedFileTinderDialog

---

## [0.3.0] — 2026-02-11

### Added
- **Double-click to open file** via `QDesktopServices::openUrl()`
- **DiagnosticTool expanded** from 13 to 22 test suites

### Fixed
- Removed minimum size height constraints (caused DPI scaling issues)
- Fixed close behavior: `QDialog::closeEvent` was accepting unconditionally
- Fixed Advanced mode startup: Removed premature `show()` + `processEvents()` before `initialize()`

---

## [0.4.0] — 2026-02-12

### Added
- **Mind map rewrite**: Vertical list replaced with horizontal grid (`QGridLayout`), configurable rows
- **Compact folder buttons** (120×28px) with drag-and-drop repositioning
- **Grid configurations** saved/loaded per root folder (`grid_configs` DB table)
- **Context menu**: Replace, Remove, Add to Quick Access, Open Folder
- **ExecutionLogEntry** with persisted `execution_log` DB table
- **Reversible deletes**: `move_to_trash()` returns trash path
- **Post-execution dialog** with per-action undo buttons
- **Launcher "Undo History"** for past executions
- **Review screen enhancements**: "Basic"/"Advanced" mode column, editable destination dropdown
- **Advanced compact toolbar**: Keep/Skip collapsed to icon buttons
- **Visual feedback**: Brief flash on progress label
- **Efficient preview loading**: `QImageReader::setScaledSize()` for large images
- **Lazy loading**: `QProgressDialog` for directories with >200 files

### Changed
- Back button removed — undo (Z/Backspace) replaces it entirely

### Fixed
- **Critical: Advanced mode freeze** — `load_folder_tree()` triggered O(N²) `refresh_layout()` calls; now batched
- **Critical: Basic mode crash on rapid clicking** — `animate_swipe()` use-after-free resolved
- **Critical: Unclosable stacked windows** — Recursive `exec()` calls fixed with `QTimer::singleShot`

---

## [0.5.0] — 2026-02-19

### Added
- **AI Mode** (`AiFileTinderDialog`): New third mode inheriting from AdvancedFileTinderDialog
  - Setup dialog with provider presets: OpenAI, Anthropic, Gemini, Mistral, Groq, OpenRouter, Ollama, LM Studio, Custom
  - **Auto mode**: AI sorts all files automatically → review screen
  - **Semi mode**: Highlights top N (2–5) folders per file with confidence scores
  - **Category handling**: Keep / Generate / Synthesize / Keep+Generate with configurable depth (1–3)
  - **Batch engine**: ~50 files/batch with taxonomy-aware prompts and 3-level JSON parse fallback
  - **Correction tracking**: User corrections fed back into subsequent batches
  - **Smart rate limiting**: Provider-specific defaults with 429 backoff
- **Qt6::Network dependency** added for AI HTTP requests
- **File List Window**: Multi-select, filter, right-click folder assignment, F key shortcut
- **Duplicate Detection Window**: Tree view grouped by name+size, MD5 hash verification, batch delete
- **Grid controls**: A-Z and # sort buttons, full path toggle, width spinner (80–300px), template presets
- **Folder color coding**: 8 preset colors via right-click context menu
- **Grid config persistence**: Save/load with `__meta__` display settings
- **Keyboard navigation**: Tab enters grid, arrows navigate, Space/Enter assigns, Escape exits
- **Copy support**: New "copy" decision alongside move/keep/delete/skip
- **Editable destinations in review**: Type paths directly, `[virtual]` tag for new folders
- **Three-way mode switching**: Switch Mode dropdown with `switch_to_ai_mode` signal
- **Dark/Light theme toggle**: Persisted in QSettings via QPalette
- **DPI scaling**: `ui::scaling::factor()` across all dialogs
- Text preview wrapped in QScrollArea
- Search cycling with wraparound
- Quick access: uniform `sizeHint` with 14-character truncation
- Cross-platform trash support (Windows SHFileOperation / macOS osascript / Linux gio)
- Full program assessment document

---

## [0.6.0] — 2026-03-01

### Added
- **Retry button on API overload**: 3-option error dialog (Retry / Continue / Abort) on AI batch failure; styled retry button for quick 429 recovery
- **Dynamic batch sizing**: `calculate_batch_size()` adapts to folder count (>30 folders = 30/batch, >15 = 40/batch), local vs cloud providers (+20 for local), and free-tier rate limits (capped at 25/batch)
- **File progress bar**: "Files classified: X / Y (Z%)" with green styling during AI analysis
- **AI reasoning label**: "AI: [reasoning]" per file in Auto and Semi modes, auto-shows/hides
- **Disabled-state tooltip on Re-run AI**: Explains how to enable when button is disabled
- **Text/list toggle in AI category review**: Switch between plain text editing and structured list view
- **Duplicate detection in category review**: Warns on duplicate category names
- **AI Setup — QScrollArea wrapping**: Scrollable content on small screens; buttons pinned at bottom
- **AI Setup — URL validation**: Red border and "Invalid URL" tooltip on invalid endpoint
- **AI Setup — Model combo persistence**: User-typed model names persist via `InsertAtBottom`
- **AI Setup — Increased purpose field**: Height raised from 45px to 70px
- **AI Setup — Test API button**: Green "Test" sends minimal request with provider-specific auth headers
- **AI Setup — Model recommendation label**: Suggests cheapest models for file sorting
- **AI Setup — Price disclaimer**: Token price estimates warning
- **AI Setup — Budget cap**: QDoubleSpinBox ($0.00–$100.00) with $0.10 steps; $0.00 = no limit
- **Review — Size column**: Human-readable file sizes via `QLocale::formattedDataSize`
- **Review — Filter/sort bar**: Filter by decision type, sort by name/decision/destination
- **Review — Decision color coding**: Green=keep, red=delete, blue=move, purple=copy, orange=skip
- **Review — Preview on hover**: Tooltip with file path, size, and MIME type
- **Review — Destination disambiguation**: Shows `parent/basename` when folder names collide
- **Review — Keyboard shortcut hint**: "Shortcuts: Ctrl+A = Select all rows"
- **Review — Styled cancel button**: Gray background, white text, rounded corners
- **Review — Performance note**: "Loading N files..." for large sets (>500)
- **Execution results — Filter combo**: Filter by action type (All/Moved/Deleted/Kept)
- **Execution results — Batch undo**: "Undo All" button with confirmation
- **Execution results — Export log**: Save execution log as CSV
- **Execution results — Disk space freed**: "Disk space freed: X MB" in session statistics
- **File List — Sort combo**: Sort by Name, Decision, or Extension
- **File List — File size display**: Items show `[K] filename.ext (1.2 KiB)`
- **File List — Decision context menu**: Keep/Delete/Skip options via right-click; `files_decision_changed` signal
- **Duplicate Detection — Column sorting** enabled on tree widget
- **Duplicate Detection — SHA-256 hashing**: Replaces MD5 for hash verification
- **Duplicate Detection — Keep Newest button**: Auto-selects all but the most recent file per group
- **Filter Widget — Visual separator**: `QFrame::VLine` between filter and sort sections
- **Filter Widget — Active filter highlight**: Blue border + tinted background on active filter
- **Filter Widget — Styled sort button**: Larger with visible border
- **Filter Widget — Clear filter button**: "X" button to reset filter
- **Mind Map — Empty state message**: "No destination folders yet. Click [+] to add folders."
- **Mind Map — Zoom controls**: Zoom in/out/fit adjusts `custom_width_`
- **Mind Map — Root button styling**: Visually larger and distinct from child buttons
- **Mind Map — Row-major layout**: Left-to-right, top-to-bottom reading order
- **Custom Extension Dialog — Batch input**: Comma/space splitting for multiple extensions
- **Custom Extension Dialog — Extension validation**: Format validation
- **Undo History Dialog — DPI-scaled sizing**
- **Undo History Dialog — Column sorting** for all columns
- **Undo History Dialog — Full paths** instead of basenames
- **Undo History Dialog — Filter combo** by action type
- **Theme-aware tooltips**: Styling respects global palette
- **Interaction discovery tooltips**: Middle-click, double-click behaviors now labeled

### Changed
- **Safer 429 decay**: `consecutive_429s_` uses `qMax(0, ...)` to prevent negative values
- File List — palette-aware theming replaces hardcoded dark theme colors
- File List — minimum size reduced from 320×400 to 280×320
- Duplicate Detection — delete button bold + brighter red (`#c0392b`) when count > 0
- Mind Map — `focused_index_` clamped with bounds checking helper
- Diagnostic tool deprecated and replaced with informational message

### Fixed
- "Session in progress" label not hiding on Clear Session (`resume_label_` set invisible after `db_manager_.clear_session()`)

---

## [Unreleased] — 2026-03-03

### Added
- **Subfolder depth control**: Spinner (0–5) with confirmation dialogs for depth changes
- **"Group by decision"** replaces sort combo in File List window
- **"Other" button** beside AI Suggestions for manual folder assignment
- **Category limit option** in AI Setup (toggle + spinner 2–50)
- **Review column click sorting** via `setSortingEnabled`
- **Visual hierarchy toggle** in grid toolbar
- **Auto Width toggle** for grid nodes
- **Sort dropdown** replaces A-Z/# buttons (Manual / A-Z / By Count)
- **Height spinner** replaces Compact/Expanded toggle (24–60px)
- **Expand grid fullscreen toggle**
- **Mode tracking per decision** (`decided_in_mode` field in database)
- **Drag-and-drop from File List to grid folder nodes** with batch support
- **Drag-and-drop from File List to keep/delete buttons** with batch support
- **Real-time file list updates** via `update_item_status`
- **Node swapping support** (`swap_nodes`) in grid view
- **Grid save/load** with session metadata and source folder prompts
- **ManualEditDialog**: Tree/text toggle, middle-click delete, batch operations, add folders/subfolders
- Quick Access hidden in AI mode; AI Suggestions always visible

- **User Data screen**: Replaces DiagnosticTool. Activity Log tab shows all execution history. AI Analysis tab uses configured AI provider to generate personality insights and file organization recommendations.
### Changed
- **"Skip" renamed to "Sort Later"** across all modes, DB schema, UI labels, keyboard shortcuts, and filters — now moves file to END of the filtered list instead of just skipping it
- **Redo functionality (Y key)** added alongside undo (Z) — standard undo/redo stack pattern
- **Review summary shows ALL files** including pending (displayed in gray `#888`)
- **Database constraint updated**: CHECK constraint now allows `'sort_later'` instead of `'skip'`
- **FileListWindow context menu**: "Skip" → "Sort Later", status prefix `[S]` → `[L]`
- **AI glow borders**: Gold `#f1c40f` glow on AI-suggested grid nodes, toggled via "Glow AI" checkbox
- **AI reasoning toggle**: "AI Reasoning" checkbox controls per-file reasoning text visibility
- **"Re-run AI" renamed to "AI Actions"**: Sub-items — "Analyze remaining files", "Re-analyze all files", "Generate new categories"
- **Confidence threshold removed** from AI Setup dialog (redundant)
- **Full path tooltips** on AI Suggestions panel items
- **3-level path display**: Cycling button — Off (basename) → Paths (relative) → Full Paths (full relative)
- **Welcome popup removed** in Advanced mode; help accessible via ? button
- **"Close" renamed to "Consolidate"** in execution results with tooltip explaining permanence
- **Undo always enabled** in execution log (removed "Permanent" disabled state)
- **Narrower undo buttons** (60px fixed width) for better table layout
- **Relative destination paths**: Shows path relative to source folder, or full path if outside
- **Review column widths**: Mode column 60px fixed, Destination stretches to fill
- **Folder picker**: Browse icon beside each destination dropdown
- **Extension-based file icons**: `[PNG]`, `[MP4]`, `[DOCX]` instead of generic `[IMG]`, `[VID]`, `[DOC]`
- **Folders sorted first** when "Include Folders" is enabled
- **Improved light theme**: Bootstrap-inspired palette with better contrast, disabled states, and link colors
- Preview toggle relocated near sort controls
- Quick access text overflow handled with elision + tooltip
- Labels audit: consistent mode titles and section headers
- Filter pipeline fix: proper compose of file type + include folders + search

### Fixed
- Preview zoom no longer reloads on filter/sort change (path caching via `current_preview_path_`)
- File List window z-order: removed `Qt::WindowStaysOnTopHint`
- Grid ghost text overlay properly hidden when folders exist (`empty_label_` tracked as member)
- Review pace accuracy: tracks session time (`session_timer_`) instead of execution time
- Execution log filter mapping: correctly maps "Moved"→"move", "Deleted"→"delete", "Kept"→"keep"
- "Session in progress" label hides correctly on Clear Session

### Removed
- `on_back()` and `show_folder_picker()` — unused methods in StandaloneFileTinderDialog
- `FolderNodeWidget` — entirely unused widget (files + CMakeLists entry)
- 15+ unused `ui_constants.hpp` entries (kNodeWidth, kNodeHeight, kAddNode sizes, `ui::icons` namespace)
- `open_diagnostics()` reference removed from launcher
