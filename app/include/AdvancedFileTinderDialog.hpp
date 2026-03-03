#ifndef ADVANCED_FILE_TINDER_DIALOG_HPP
#define ADVANCED_FILE_TINDER_DIALOG_HPP

#include "StandaloneFileTinderDialog.hpp"
#include <QListWidget>
#include <QScrollArea>
#include <QSet>

class FolderTreeModel;
class MindMapView;
class FilterWidget;

class AdvancedFileTinderDialog : public StandaloneFileTinderDialog {
    Q_OBJECT

public:
    explicit AdvancedFileTinderDialog(const QString& source_folder,
                                       DatabaseManager& db,
                                       QWidget* parent = nullptr);
    ~AdvancedFileTinderDialog() override;
    
    void initialize() override;
    
protected:
    // Advanced mode components accessible to derived classes (AI Mode)
    MindMapView* mind_map_view_;
    QListWidget* quick_access_list_;
    FolderTreeModel* folder_model_;
    QWidget* quick_access_panel_;

private:
    // Advanced mode internal components
    QScrollArea* scroll_area_;
    QWidget* main_content_;
    QWidget* file_info_panel_;
    QLabel* adv_file_icon_label_;
    QLabel* file_name_label_;
    QLabel* file_details_label_;
    QLabel* adv_preview_label_;     // Small inline image preview
    FilterWidget* filter_widget_;
    
    // Quick access management
    QStringList quick_access_folders_;
    static const int kMaxQuickAccess = 10;
    
    // UI setup
    void setup_ui() override;
    void setup_filter_bar();
    void setup_mind_map();
    void setup_file_info_panel();
    void setup_quick_access_panel();
    void setup_action_buttons();
    
    // Folder operations
    void on_folder_clicked(const QString& folder_path);
    void on_add_node_clicked();
    void prompt_add_folder();
    void on_folder_context_menu(const QString& folder_path, const QPoint& pos);
    
    // Quick access
    void load_quick_access();
    void save_quick_access();
    void add_to_quick_access(const QString& folder_path);
    void remove_from_quick_access(int index);
    void update_quick_access_display();
    void on_quick_access_clicked(QListWidgetItem* item);
    void on_add_quick_access();
    void on_remove_quick_access();
    
    // Grid configuration
    void save_grid_config();
    void load_grid_config();
    void reset_grid();
    QStringList get_destination_folders() const override;
    
    // Folder exclusion — folders in grid/quick access are exempt from processing
    QSet<QString> get_excluded_folder_paths() const;
    
    // File info display
    void update_file_info_display();
    QString get_file_type_icon(const QString& path);
    
    // Override actions
    void on_finish() override;
    void on_undo() override;

protected:
    // show_current_file accessible to AI mode for override chaining
    void show_current_file() override;

private:
    // Event handlers
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    
    // Keyboard shortcuts
    void keyPressEvent(QKeyEvent* event) override;
    
    // Filter/sort handling
    void on_filter_changed();
    void on_sort_changed();
    
    // Load/save
    void load_folder_tree();
    void save_folder_tree();
    
    // Session checks
    void check_missing_folders();
    
signals:
    void switch_to_basic_mode();
};

#endif // ADVANCED_FILE_TINDER_DIALOG_HPP
