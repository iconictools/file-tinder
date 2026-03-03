# Changelog

All notable changes to File Tinder are documented in this file.

## [Unreleased] — 2026-03-02

### Added — AI Mode Enhancements
- **Retry button on API overload**: 3-option error dialog (Retry / Continue / Abort) when AI batch fails. Styled retry button for quick recovery from 429 rate limits.
- **Dynamic batch sizing**: Replaced fixed 50-file batches with `calculate_batch_size()` that adapts to folder count (>30 folders = 30/batch, >15 = 40/batch), local vs cloud providers (+20 for local), and free-tier rate limits (capped at 25/batch).
- **File progress bar**: Shows "Files classified: X / Y (Z%)" with green styling during AI analysis.
- **AI reasoning label**: Displays "AI: [reasoning]" for the current file when the AI provides a reason for its suggestion. Visible in both Auto and Semi modes. Auto-shows/hides per file.
- **Disabled-state tooltip on Re-run AI**: Tooltip explains "Configure AI provider in 'AI Setup' to enable re-running" when the button is disabled.
- **Safer 429 decay**: `consecutive_429s_` now uses `qMax(0, ...)` to prevent negative edge case.
- **Text/list toggle in AI category review**: Users can toggle between plain text editing and structured list view with add/remove buttons for AI-suggested categories.
- **Duplicate detection in category review**: Warns if user types the same category twice.

### Added — AI Setup Dialog Enhancements
- **QScrollArea wrapping**: Dialog content scrolls on small screens; buttons pinned at bottom.
- **URL validation**: Endpoint field shows red border and "Invalid URL" tooltip when scheme is missing/invalid.
- **Model combo persistence**: Changed from `NoInsert` to `InsertAtBottom` so user-typed model names persist in dropdown.
- **Increased purpose field**: Text field height raised from 45px to 70px for multi-line descriptions.
- **Test API button**: Green "Test" button sends minimal "Say OK" request to configured endpoint with provider-specific auth headers. Shows success/failure with HTTP status.
- **Model recommendation label**: "Recommended: use the cheapest model (gpt-4o-mini, claude-3-haiku, gemini-1.5-flash, llama-3.1-8b) for file sorting."
- **Price disclaimer**: "(Token prices are estimates and may change. Check your provider dashboard for exact costs.)"
- **Budget cap**: QDoubleSpinBox ($0.00–$100.00) with $0.10 steps. $0.00 = no limit.

### Added — Review Screen Enhancements
- **Size column**: Human-readable file sizes via `QLocale::formattedDataSize` in a new column.
- **Filter/sort bar**: Filter by decision type (All/Keep/Delete/Skip/Move/Copy/Pending) and sort (Original/By Name/By Decision/By Destination).
- **Decision-based color coding**: File names colorized by decision — green=keep, red=delete, blue=move, purple=copy, orange=skip.
- **Preview on hover**: Toggle checkbox; hovering rows shows tooltip with file path, size, and MIME type.
- **Destination disambiguation**: When multiple folders share a basename, combo shows `parent/basename` instead of just `basename`.
- **Keyboard shortcut hint**: "Shortcuts: Ctrl+A = Select all rows" at bottom.
- **Styled cancel button**: Gray background (#7f8c8d), white text, rounded corners.
- **Performance note**: For large file sets (>500), shows "Loading N files..." with `processEvents()`.

### Added — Execution Results Enhancements
- **Filter combo**: Filter execution log by action type (All/Moved/Deleted/Kept).
- **Batch undo**: "Undo All" button clicks all enabled undo buttons after confirmation.
- **Export log**: "Export Log" button saves execution log as CSV via file dialog.
- **Disk space freed**: Summary shows "Disk space freed: X MB" in red within session statistics.

### Added — File List Window Enhancements
- **Palette-aware theming**: Removed hardcoded dark theme colors; uses `palette(highlight)` and `palette(highlighted-text)`.
- **Sort combo**: Sort by Name, Decision, or Extension.
- **File size display**: Items now show `[K] filename.ext (1.2 KiB)`.
- **Decision changes from context menu**: Right-click menu includes Keep/Delete/Skip options alongside folder assignment. New `files_decision_changed` signal.
- **Smaller minimum size**: Reduced from 320x400 to 280x320.

### Added — Duplicate Detection Enhancements
- **Column sorting**: Enabled `setSortingEnabled(true)` on tree widget.
- **SHA-256 hashing**: Replaced MD5 with SHA-256 for hash verification. All labels updated.
- **Keep Newest button**: Selects all duplicates except the most recently modified file in each group for deletion.
- **Delete button emphasis**: Bold + brighter red (#c0392b) when count > 0; normal when 0.

### Added — Filter Widget UI
- **Visual separator**: `QFrame::VLine` between filter and sort sections (replaces spacer).
- **Active filter highlight**: Blue border + blue-tinted background when non-"All" filter is active.
- **Styled sort button**: Larger with visible border for clearer clickability.
- **Clear filter button**: "X" button to reset filter to "All".

### Added — Mind Map / Grid View Enhancements
- **Empty state message**: Shows "No destination folders yet. Click [+] to add folders." when grid is empty.
- **Zoom controls**: Zoom in/out/fit adjusts `custom_width_` for the widget-based grid.
- **Root button styling**: Root button is visually larger and distinct from child buttons.
- **Row-major layout**: Alternative left-to-right, top-to-bottom reading order.
- **Bounds checking**: `focused_index_` clamped with helper when folder list changes during navigation.

### Added — Custom Extension Dialog
- **Styled buttons**: Improved Add button styling and extension list appearance.
- **Batch input**: Comma/space splitting for pasting multiple extensions at once (e.g., "txt, csv, log").
- **Extension validation**: Format validation for typed extensions.

### Added — Undo History Dialog
- **DPI-scaled dialog**: Proper sizing based on screen DPI.
- **Column sorting**: Enabled for all columns.
- **Full paths**: Shows complete file paths instead of basenames.
- **Filter combo**: Filter by action type.

### Added — Cross-cutting Improvements
- **Theme-aware tooltips**: Tooltip styling respects global palette.
- **Interaction discovery tooltips**: Middle-click to remove from Quick Access, double-click-to-open-file — these interactions are now labeled.
- **Diagnostic tool deprecated**: Replaced with informational message directing users to alternative resources.

### Fixed
- **"Session in progress" label not hiding on Clear Session**: `resume_label_` now set to invisible after `db_manager_.clear_session()`.

### Assessment
- Full screen-by-screen assessment of all 17 screens/dialogs with UI Design and Feature grades (see `assessment_screen_ratings`).
- Comprehensive program assessment covering architecture, code quality, feature completeness, security, performance, and prioritized action items (see `assessment`).

## [0.1.0] — Initial Release

### Core Features
- **Three-mode file sorting**: Basic (swipe-style), Advanced (visual mind map grid), AI (AI-assisted categorization).
- **Basic Mode**: Arrow key sorting (→ Keep, ← Delete, ↓ Skip), undo (Z/Backspace), image preview (P), file list (F), search (Ctrl+F), finish review (Enter), help (?).
- **Advanced Mode**: Clickable folder grid with drag-drop reposition, keyboard navigation (Tab to enter, arrows to navigate, Space/Enter to assign, Escape to exit), quick access bar (1-9, 0), folder creation (N), context menu with color coding (8 preset colors), grid configs save/load, template presets.
- **AI Mode**: Auto and Semi sorting modes, multi-provider support (OpenAI, Anthropic, Gemini, Mistral, Groq, OpenRouter, Ollama, LM Studio, Custom), taxonomy-aware prompting, category review dialog, correction tracking, smart rate limiting with 429 backoff.
- **Review & Execute**: Pre-execution review with editable decisions, bulk actions, virtual folder creation, copy support, safe trash deletion, per-action undo.
- **Execution Results**: Session statistics, review pace metric, per-action undo buttons, error reporting.
- **File List Window**: Tool window with filter, multi-select, right-click folder assignment, decision status display.
- **Image Preview**: Zoomable preview window with keyboard navigation, fit/actual size modes, Ctrl+Wheel zoom.
- **Duplicate Detection**: Name+size matching, SHA-256 hash verification, multi-select deletion, integration with review flow.
- **Filter Widget**: 9 filter types, 4 sort fields, ascending/descending toggle, custom extension filter, include folders toggle.
- **Session Persistence**: SQLite database with 8 tables, resume across restarts, recent folders history.
- **Theme Support**: Dark/Light theme toggle via QPalette, persisted in QSettings.
- **DPI Scaling**: Consistent scaling via `ui::scaling::factor()` across all dialogs.
- **Cross-platform**: Windows (SHFileOperation), macOS (osascript), Linux (gio) trash support.
