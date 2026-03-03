#ifndef MIND_MAP_VIEW_HPP
#define MIND_MAP_VIEW_HPP

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include <QString>
#include <QPoint>

class FolderTreeModel;
class FolderNode;

// Folder cell used in the mind map grid
class FolderButton : public QPushButton {
    Q_OBJECT

public:
    FolderButton(FolderNode* node, QWidget* parent = nullptr);
    
    FolderNode* node() const { return node_; }
    void set_selected(bool selected);
    void update_display();
    void set_path_display_mode(int mode, const QString& source_path = {}) {
        path_display_mode_ = mode; source_path_ = source_path; update_display();
    }
    void set_highlighted(bool on);
    
signals:
    void folder_clicked(const QString& path);
    void folder_right_clicked(const QString& path, const QPoint& global_pos);
    void drag_started(const QString& path);
    void files_dropped(const QString& folder_path, QList<int> file_indices);
    
protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    
private:
    FolderNode* node_;
    bool is_selected_;
    QPoint drag_start_pos_;
    int path_display_mode_ = 0;  // 0=basename, 1=relative, 2=full relative
    QString source_path_;
    bool is_highlighted_ = false;
    
    void update_style();
};

// Grid-based mind map view for folder destinations
class MindMapView : public QWidget {
    Q_OBJECT

public:
    explicit MindMapView(QWidget* parent = nullptr);
    ~MindMapView() override;
    
    void set_model(FolderTreeModel* model);
    void refresh_layout();
    void zoom_in();
    void zoom_out();
    void zoom_fit();
    void set_selected_folder(const QString& path);
    
signals:
    void folder_clicked(const QString& path);
    void folder_double_clicked(const QString& path);
    void folder_context_menu(const QString& path, const QPoint& global_pos);
    void add_folder_requested();
    void add_subfolder_requested(const QString& parent_path);
    void files_dropped(const QString& folder_path, QList<int> file_indices);
    
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    
private:
    QScrollArea* scroll_area_;
    QWidget* content_widget_;
    QGridLayout* grid_layout_;
    FolderTreeModel* model_;
    QMap<QString, FolderButton*> buttons_;
    QPushButton* add_button_;
    
    // Grid position tracking: maps folder path to (row, col)
    QMap<QString, QPair<int, int>> grid_positions_;
    int next_row_;
    int next_col_;
    int max_rows_per_col_ = 6;  // Configurable items per column before wrapping
    bool compact_mode_ = true;  // Compact (small) vs expanded (wider) folder buttons
    int path_display_mode_ = 0;  // 0=off, 1=paths, 2=full paths
    int custom_width_ = 0;  // 0 = use compact/expanded defaults
    bool auto_width_ = false;  // Auto-size buttons to fit text content
    int custom_height_ = 32;  // Custom button height (24-60)
    bool row_major_ = false;  // false = column-major (top-to-bottom), true = row-major (left-to-right)
    bool show_hierarchy_ = false;  // Indent subfolders by depth
    
    void build_grid();
    void place_folder_node(FolderNode* node);
    
public:
    void set_max_rows_per_col(int rows) { max_rows_per_col_ = qMax(1, rows); }
    int max_rows_per_col() const { return max_rows_per_col_; }
    void set_compact_mode(bool compact) { compact_mode_ = compact; }
    bool compact_mode() const { return compact_mode_; }
    void set_path_display_mode(int mode) { path_display_mode_ = mode; }
    int path_display_mode() const { return path_display_mode_; }
    void set_custom_width(int w) { custom_width_ = w; }
    int custom_width() const { return custom_width_; }
    void set_row_major(bool rm) { row_major_ = rm; }
    bool row_major() const { return row_major_; }
    void set_show_hierarchy(bool h) { show_hierarchy_ = h; build_grid(); }
    bool show_hierarchy() const { return show_hierarchy_; }
    void set_auto_width(bool a) { auto_width_ = a; build_grid(); }
    bool auto_width() const { return auto_width_; }
    void set_custom_height(int h) { custom_height_ = h; build_grid(); }
    int custom_height() const { return custom_height_; }
    void sort_alphabetically();
    void sort_by_count();

    // AI glow highlighting
    void set_highlighted_paths(const QStringList& paths);
    void clear_highlighted_paths();

    // Keyboard navigation
    void set_keyboard_mode(bool on);
    bool keyboard_mode() const { return keyboard_mode_; }
    void focus_next();
    void focus_prev();
    void focus_up();
    void focus_down();
    void activate_focused();  // Emits folder_clicked for the focused button
    QString focused_folder_path() const;

private:
    bool keyboard_mode_ = false;
    int focused_index_ = -1;       // Index into ordered_paths_
    QStringList ordered_paths_;     // Paths in grid order (row-major by column)
    QLabel* empty_label_ = nullptr; // Empty state message
    QStringList highlighted_paths_;  // AI glow highlight paths
    void update_focus_visual();
    void build_ordered_paths();
    void clamp_focused_index();
};

#endif // MIND_MAP_VIEW_HPP
