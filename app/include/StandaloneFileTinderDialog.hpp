#ifndef STANDALONE_FILE_TINDER_DIALOG_HPP
#define STANDALONE_FILE_TINDER_DIALOG_HPP

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <vector>
#include <memory>

class DatabaseManager;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class ImagePreviewWindow;
class FileListWindow;
struct ExecutionResult;

// Action record for undo functionality
struct ActionRecord {
    int file_index;           // Index into files_ vector
    QString previous_decision; // What the decision was before
    QString new_decision;      // What we changed it to
    QString destination_folder; // For move operations
};

struct FileToProcess {
    QString path;
    QString name;
    QString extension;
    qint64 size;
    QString modified_date;
    QDateTime modified_datetime;  // For sorting
    QString decision;           // "pending", "keep", "delete", "sort_later", "move"
    QString destination_folder; // For move operations
    QString mime_type;          // MIME type for filtering
    QString decided_in_mode;    // Which mode made the decision
    bool is_directory;          // For folder support
    bool has_duplicate = false; // Cached: another file shares same name+size
};

// File filter types
enum class FileFilterType {
    All,
    Images,
    Videos,
    Audio,
    Documents,
    Archives,
    Other,
    FoldersOnly,    // New: folders only
    Custom          // New: custom extensions
};

// Sort options
enum class FileSortField {
    Name,
    Size,
    Type,
    DateModified
};

enum class SortOrder {
    Ascending,
    Descending
};

class StandaloneFileTinderDialog : public QWidget {
    Q_OBJECT

public:
    explicit StandaloneFileTinderDialog(const QString& source_folder,
                                         DatabaseManager& db,
                                         QWidget* parent = nullptr,
                                         const QStringList& additional_sources = {});
    ~StandaloneFileTinderDialog() override;
    
    // Initialize the dialog - must be called after construction
    // This allows derived classes to properly initialize before UI setup
    virtual void initialize();
    
    const QString& source_folder() const { return source_folder_; }
    
protected:
    // File management
    std::vector<FileToProcess> files_;
    std::vector<int> filtered_indices_;  // Indices into files_ after filtering
    int current_filtered_index_;         // Current position in filtered list
    QString source_folder_;
    QStringList source_folders_;  // All source folders (including source_folder_)
    DatabaseManager& db_;
    FileFilterType current_filter_;
    
    // Sorting
    FileSortField sort_field_;
    SortOrder sort_order_;
    
    // Mode tracking
    QString mode_name_ = "Basic";
    
    // Custom filter
    QStringList custom_extensions_;
    bool include_folders_;
    
    // Statistics
    int keep_count_;
    int delete_count_;
    int sort_later_count_;
    int move_count_;
    int copy_count_ = 0;
    
    // Undo/redo stacks
    std::vector<ActionRecord> undo_stack_;
    std::vector<ActionRecord> redo_stack_;
    
    // Image preview window (for separate window mode)
    ImagePreviewWindow* image_preview_window_;

    // File list window (for real-time status updates)
    QPointer<FileListWindow> file_list_window_;
    
    // UI Components
    QLabel* preview_label_;
    QLabel* file_info_label_;
    QLabel* file_icon_label_;      // Centered file icon
    QLabel* progress_label_;
    QLabel* stats_label_;
    QProgressBar* progress_bar_;
    QComboBox* filter_combo_;
    QComboBox* sort_combo_;        // Sort field selector
    QPushButton* sort_order_btn_;  // Asc/Desc toggle
    QCheckBox* folders_checkbox_;  // Include folders toggle
    QSpinBox* subfolder_depth_spin_ = nullptr;  // Subfolder recursion depth
    QLabel* shortcuts_label_;
    QLabel* file_position_label_;
    QLabel* size_badge_label_;
    QLineEdit* search_box_;
    
    QPushButton* delete_btn_;
    QPushButton* sort_later_btn_;
    QPushButton* keep_btn_;
    QPushButton* undo_btn_;        // Undo button (replaces move_btn_)
    QPushButton* redo_btn_;        // Redo button
    QPushButton* preview_btn_;     // Image preview in separate window
    QPushButton* finish_btn_;
    QPushButton* switch_mode_btn_;
    QPushButton* help_btn_;
    QPushButton* duplicate_btn_ = nullptr;  // Duplicate detection ! button
    
    // Animation
    QPropertyAnimation* swipe_animation_;
    
    // Resize debounce timer
    QTimer* resize_timer_;
    
    // Preview caching — avoid reloading same file on filter/sort changes
    QString current_preview_path_;
    
    // Close guard to prevent re-entrant close
    bool closing_ = false;
    bool animating_ = false;
    
    // Session timing for review pace
    QElapsedTimer session_timer_;
    
    // Initialization
    virtual void setup_ui();
    void scan_files();
    void load_session_state();
    void save_session_state();
    void save_last_folder();      // New: persist last used folder
    QString get_last_folder();    // New: retrieve last used folder
    
    // Filtering
    void apply_filter(FileFilterType filter);
    void rebuild_filtered_indices();
    bool file_matches_filter(const FileToProcess& file) const;
    void show_custom_extension_dialog();  // New: custom extension picker
    
    // Sorting
    void apply_sort();
    void on_sort_changed(int index);
    void on_sort_order_toggled();
    void on_folders_toggle_changed(int state);
    
    // File display
    virtual void show_current_file();
    void update_preview(const QString& file_path);
    void update_file_info(const FileToProcess& file);
    void update_progress();
    void update_stats();
    
    // Animation
    void animate_swipe(bool forward);
    
    // Actions
    virtual void on_keep();
    virtual void on_delete();
    virtual void on_sort_later();
    virtual void on_search(const QString& text);
    virtual void on_undo();           // Undo last action
    virtual void on_redo();           // Redo last undone action
    virtual void on_show_preview();   // Open image in separate window
    virtual void on_finish();
    void advance_to_next();
    void go_to_previous();
    void record_action(int file_index, const QString& old_decision, const QString& new_decision,
                       const QString& dest_folder = QString());
    
    // Helper to update decision counts (deduplication)
    void update_decision_count(const QString& old_decision, int delta);
    int get_current_file_index() const;  // Get actual file index from filtered index
    
    // File list window helper — single creation point for button & F key
    void open_file_list_window();
    
    // Review screen
    void show_review_summary();
    void execute_decisions();
    void show_execution_results(const ExecutionResult& result, qint64 elapsed_ms);
    virtual QStringList get_destination_folders() const;  // Grid folders for review dropdown
    
    // Help
    void show_shortcuts_help();
    
    // Reset progress
    void on_reset_progress();
    
    // Keyboard shortcuts
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;  // New: handle resize
    void request_close();  // Handles save-prompt logic, emits request_back
    bool eventFilter(QObject* obj, QEvent* event) override;  // Double-click to open file
    
signals:
    void session_completed();
    void switch_to_advanced_mode();
    void switch_to_ai_mode();
    void request_back();
    
protected slots:
    void on_switch_mode_clicked();
    void on_filter_changed(int index);
};

#endif // STANDALONE_FILE_TINDER_DIALOG_HPP
