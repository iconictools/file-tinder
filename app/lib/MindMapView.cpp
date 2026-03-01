#include "MindMapView.hpp"
#include "FolderTreeModel.hpp"
#include "ui_constants.hpp"
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QDir>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>

// === FolderButton Implementation ===

FolderButton::FolderButton(FolderNode* node, QWidget* parent)
    : QPushButton(parent)
    , node_(node)
    , is_selected_(false) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(ui::scaling::scaled(120), ui::scaling::scaled(28));
    update_display();
    update_style();
    
    connect(this, &QPushButton::clicked, this, [this]() {
        emit folder_clicked(node_->path);
    });
}

void FolderButton::update_display() {
    QString name;
    if (show_full_path_) {
        name = node_->path;
        QStringList parts = name.split(QDir::separator());
        if (parts.size() <= 2) parts = name.split('/');  // fallback for stored paths
        if (parts.size() > 2) {
            name = parts.mid(parts.size() - 2).join(QDir::separator());
        }
        // For single-component paths, just use the full path as-is
    } else {
        name = node_->display_name;
        if (name.isEmpty()) {
            name = QFileInfo(node_->path).fileName();
        }
    }
    
    // Compact display: name + count
    QString count_str;
    if (node_->assigned_file_count > 0) {
        count_str = QString(" (%1)").arg(node_->assigned_file_count);
    }
    
    // Truncate long names to fit button width (~9px per character at font-size 10px)
    int max_len = qMax(8, width() / 9);
    if (name.length() > max_len) {
        name = name.left(max_len - 1) + "…";
    }
    
    setText(name + count_str);
    setToolTip(node_->path);
}

void FolderButton::set_selected(bool selected) {
    is_selected_ = selected;
    update_style();
}

void FolderButton::update_style() {
    if (is_selected_) {
        setStyleSheet(
            "QPushButton { text-align: center; padding: 2px 6px; "
            "background-color: #1a3a5c; border: 2px solid #3498db; "
            "border-radius: 4px; color: #3498db; font-weight: bold; font-size: 10px; }"
            "QPushButton:hover { background-color: #1e4a6e; }"
        );
    } else if (!node_->custom_color.isEmpty()) {
        // User-assigned custom color
        QColor c(node_->custom_color);
        QColor bg = c.darker(300);
        setStyleSheet(QString(
            "QPushButton { text-align: center; padding: 2px 6px; "
            "background-color: %1; border: 1px solid %2; "
            "border-radius: 4px; color: %2; font-size: 10px; }"
            "QPushButton:hover { background-color: %3; }"
        ).arg(bg.name(), c.name(), bg.lighter(120).name()));
    } else if (!node_->exists) {
        setStyleSheet(
            "QPushButton { text-align: center; padding: 2px 6px; "
            "background-color: #3a3520; border: 1px dashed #f39c12; "
            "border-radius: 4px; color: #f39c12; font-size: 10px; }"
            "QPushButton:hover { background-color: #4a4530; }"
        );
    } else if (node_->is_external) {
        setStyleSheet(
            "QPushButton { text-align: center; padding: 2px 6px; "
            "background-color: #2d1f3d; border: 1px solid #9b59b6; "
            "border-radius: 4px; color: #bb6bd9; font-size: 10px; }"
            "QPushButton:hover { background-color: #3d2f4d; }"
        );
    } else {
        setStyleSheet(
            "QPushButton { text-align: center; padding: 2px 6px; "
            "background-color: #34495e; border: 1px solid #4a6078; "
            "border-radius: 4px; color: #ecf0f1; font-size: 10px; }"
            "QPushButton:hover { background-color: #3d566e; border-color: #5a7a98; }"
        );
    }
}

void FolderButton::contextMenuEvent(QContextMenuEvent* event) {
    emit folder_right_clicked(node_->path, event->globalPos());
    event->accept();
}

void FolderButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_start_pos_ = event->pos();
    }
    QPushButton::mousePressEvent(event);
}

void FolderButton::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - drag_start_pos_).manhattanLength() < 20) return;
    
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    mime->setText(node_->path);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

// === MindMapView Implementation ===

MindMapView::MindMapView(QWidget* parent)
    : QWidget(parent)
    , scroll_area_(new QScrollArea(this))
    , content_widget_(nullptr)
    , grid_layout_(nullptr)
    , model_(nullptr)
    , add_button_(nullptr)
    , next_row_(0)
    , next_col_(0) {
    
    auto* outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);
    
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet("QScrollArea { background-color: #2c3e50; border-radius: 4px; }");
    outer_layout->addWidget(scroll_area_);
    
    setAcceptDrops(true);
    setMinimumHeight(ui::scaling::scaled(120));
}

MindMapView::~MindMapView() = default;

void MindMapView::set_model(FolderTreeModel* model) {
    model_ = model;
    
    if (model_) {
        connect(model_, &FolderTreeModel::folder_structure_changed, 
                this, &MindMapView::refresh_layout);
        
        refresh_layout();
    }
}

void MindMapView::refresh_layout() {
    if (!model_) return;
    
    // Clear existing
    buttons_.clear();
    add_button_ = nullptr;
    grid_positions_.clear();
    next_row_ = 0;
    next_col_ = 0;
    
    // Create new content widget
    content_widget_ = new QWidget();
    content_widget_->setStyleSheet("background-color: #2c3e50;");
    grid_layout_ = new QGridLayout(content_widget_);
    grid_layout_->setContentsMargins(6, 6, 6, 6);
    grid_layout_->setSpacing(4);
    
    // Build the horizontal grid
    build_grid();
    
    // Add the "+" button in the next available slot
    add_button_ = new QPushButton("+");
    add_button_->setFixedSize(ui::scaling::scaled(28), ui::scaling::scaled(28));
    add_button_->setCursor(Qt::PointingHandCursor);
    add_button_->setStyleSheet(
        "QPushButton { text-align: center; padding: 2px; "
        "background-color: #27ae60; border: none; "
        "border-radius: 4px; color: white; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    connect(add_button_, &QPushButton::clicked, this, &MindMapView::add_folder_requested);
    grid_layout_->addWidget(add_button_, next_row_, next_col_);

    // Show empty state message if no child folders exist
    if (!model_ || !model_->root_node() || model_->root_node()->children.empty()) {
        auto* empty_label = new QLabel("No destination folders yet.\nClick [+] to add folders.", content_widget_);
        empty_label->setStyleSheet("color: #888; font-size: 13px; font-style: italic;");
        empty_label->setAlignment(Qt::AlignCenter);
        empty_label->move(200, 50);
        empty_label->show();
    }
    
    // Set the new content widget
    scroll_area_->setWidget(content_widget_);

    // Rebuild keyboard navigation order if in keyboard mode
    if (keyboard_mode_) {
        build_ordered_paths();
        if (focused_index_ >= ordered_paths_.size()) {
            focused_index_ = ordered_paths_.isEmpty() ? -1 : 0;
        }
        update_focus_visual();
    }
}

void MindMapView::build_grid() {
    if (!model_ || !model_->root_node()) return;
    
    FolderNode* root = model_->root_node();
    
    // Button dimensions based on display mode
    int btn_w = custom_width_ > 0 ? ui::scaling::scaled(custom_width_)
                                   : (compact_mode_ ? ui::scaling::scaled(120) : ui::scaling::scaled(180));
    int btn_h = compact_mode_ ? ui::scaling::scaled(32) : ui::scaling::scaled(36);
    int font_size = compact_mode_ ? 11 : 12;
    
    // Root folder in column 0, spanning all rows that children will use
    int root_w = compact_mode_ ? ui::scaling::scaled(140) : ui::scaling::scaled(200);
    int root_h = compact_mode_ ? ui::scaling::scaled(38) : ui::scaling::scaled(42);
    if (custom_width_ > 0) root_w = ui::scaling::scaled(custom_width_ + 20);
    auto* root_btn = new FolderButton(root, content_widget_);
    root_btn->set_show_full_path(show_full_paths_);
    root_btn->setFixedSize(root_w, root_h);
    root_btn->setStyleSheet(
        QString("QPushButton { text-align: center; padding: 2px 6px; "
        "background-color: #1a252f; border: 2px solid #3498db; "
        "border-radius: 4px; color: #3498db; font-weight: bold; font-size: %1px; }"
        "QPushButton:hover { background-color: #1e2f3d; }").arg(font_size)
    );
    buttons_[root->path] = root_btn;
    grid_positions_[root->path] = {0, 0};
    
    connect(root_btn, &FolderButton::folder_clicked, this, &MindMapView::folder_clicked);
    connect(root_btn, &FolderButton::folder_right_clicked, this, &MindMapView::folder_context_menu);
    
    // Child folders fill columns 1+ going down, wrapping to next column
    next_row_ = 0;
    next_col_ = 1;
    
    for (const auto& child : root->children) {
        place_folder_node(child.get());
    }
    
    // Root spans all rows used by children. If next_col_ moved past col 1,
    // the current next_row_ is partial (items still being placed), so add 1.
    int total_rows = qMax(1, next_row_ + (next_col_ > 1 ? 1 : 0));
    grid_layout_->addWidget(root_btn, 0, 0, total_rows, 1, Qt::AlignVCenter);
}

void MindMapView::place_folder_node(FolderNode* node) {
    auto* btn = new FolderButton(node, content_widget_);
    btn->set_show_full_path(show_full_paths_);
    
    // Apply display mode sizing: ensure uniform row height in both modes
    if (compact_mode_) {
        int w = custom_width_ > 0 ? ui::scaling::scaled(custom_width_) : ui::scaling::scaled(120);
        btn->setFixedSize(w, ui::scaling::scaled(32));
    } else {
        int w = custom_width_ > 0 ? ui::scaling::scaled(custom_width_) : ui::scaling::scaled(180);
        btn->setFixedSize(w, ui::scaling::scaled(36));
    }
    
    buttons_[node->path] = btn;
    grid_positions_[node->path] = {next_row_, next_col_};
    grid_layout_->addWidget(btn, next_row_, next_col_);
    
    connect(btn, &FolderButton::folder_clicked, this, &MindMapView::folder_clicked);
    connect(btn, &FolderButton::folder_right_clicked, this, &MindMapView::folder_context_menu);
    
    // Advance position based on layout direction
    if (row_major_) {
        // Row-major: fill left-to-right, then wrap down
        next_col_++;
        if (next_col_ >= max_rows_per_col_ + 1) {  // +1 to account for root column
            next_col_ = 1;
            next_row_++;
        }
    } else {
        // Column-major: fill top-to-bottom, then wrap right
        next_row_++;
        if (next_row_ >= max_rows_per_col_) {
            next_row_ = 0;
            next_col_++;
        }
    }
    
    // Place children (they go into the grid too)
    for (const auto& child : node->children) {
        place_folder_node(child.get());
    }
}

void MindMapView::zoom_in() {
    if (custom_width_ < 300) {
        custom_width_ += 20;
        refresh_layout();
    }
}

void MindMapView::zoom_out() {
    if (custom_width_ > 80) {
        custom_width_ -= 20;
        refresh_layout();
    }
}

void MindMapView::zoom_fit() {
    custom_width_ = compact_mode_ ? 120 : 180;
    refresh_layout();
}

void MindMapView::set_selected_folder(const QString& path) {
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
        it.value()->set_selected(it.key() == path);
    }
}

void MindMapView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void MindMapView::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasText() || !model_ || !grid_layout_) return;
    
    QString dragged_path = event->mimeData()->text();
    
    // Find the button under the drop position to swap with
    QWidget* target_widget = content_widget_->childAt(
        content_widget_->mapFrom(this, event->position().toPoint()));
    auto* target_btn = qobject_cast<FolderButton*>(target_widget);
    
    if (!target_btn || target_btn->node()->path == dragged_path) return;
    if (target_btn->node()->path == model_->root_node()->path) return;  // Can't swap with root
    
    QString target_path = target_btn->node()->path;
    
    // Swap positions in the grid
    if (grid_positions_.contains(dragged_path) && grid_positions_.contains(target_path)) {
        auto pos_a = grid_positions_[dragged_path];
        auto pos_b = grid_positions_[target_path];
        
        // Remove both from layout
        grid_layout_->removeWidget(buttons_[dragged_path]);
        grid_layout_->removeWidget(buttons_[target_path]);
        
        // Re-add in swapped positions
        grid_layout_->addWidget(buttons_[dragged_path], pos_b.first, pos_b.second);
        grid_layout_->addWidget(buttons_[target_path], pos_a.first, pos_a.second);
        
        // Update position map
        grid_positions_[dragged_path] = pos_b;
        grid_positions_[target_path] = pos_a;
    }
    
    event->acceptProposedAction();
}

void MindMapView::sort_alphabetically() {
    if (!model_ || !model_->root_node()) return;
    model_->sort_children_alphabetically(model_->root_node());
    refresh_layout();
}

void MindMapView::sort_by_count() {
    if (!model_ || !model_->root_node()) return;
    model_->sort_children_by_count(model_->root_node());
    refresh_layout();
}

void MindMapView::set_keyboard_mode(bool on) {
    keyboard_mode_ = on;
    if (on) {
        build_ordered_paths();
        if (!ordered_paths_.isEmpty() && focused_index_ < 0) {
            focused_index_ = 0;
        }
        update_focus_visual();
    } else {
        focused_index_ = -1;
        // Clear focus ring from all buttons
        for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
            it.value()->set_selected(false);
        }
    }
}

void MindMapView::build_ordered_paths() {
    // Build a list of folder paths in grid order (column-major: col 1 top to bottom, then col 2, etc.)
    // Skip the root (col 0) — it's not a sorting target
    ordered_paths_.clear();

    // Collect (col, row, path) tuples
    QList<std::tuple<int, int, QString>> entries;
    for (auto it = grid_positions_.begin(); it != grid_positions_.end(); ++it) {
        int row = it.value().first;
        int col = it.value().second;
        if (col == 0) continue;  // Skip root
        entries.append({col, row, it.key()});
    }

    // Sort by column first, then row
    std::sort(entries.begin(), entries.end());

    for (const auto& [col, row, path] : entries) {
        ordered_paths_.append(path);
    }
}

void MindMapView::update_focus_visual() {
    if (!keyboard_mode_) return;

    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
        it.value()->set_selected(false);
    }

    if (focused_index_ >= 0 && focused_index_ < ordered_paths_.size()) {
        QString path = ordered_paths_[focused_index_];
        if (buttons_.contains(path)) {
            buttons_[path]->set_selected(true);
            // Ensure the focused button is visible in the scroll area
            buttons_[path]->ensurePolished();
            scroll_area_->ensureWidgetVisible(buttons_[path]);
        }
    }
}

void MindMapView::clamp_focused_index() {
    if (focused_index_ < 0) focused_index_ = 0;
    if (focused_index_ >= static_cast<int>(ordered_paths_.size()))
        focused_index_ = static_cast<int>(ordered_paths_.size()) - 1;
}

void MindMapView::focus_next() {
    if (ordered_paths_.isEmpty()) return;
    focused_index_ = (focused_index_ + 1) % ordered_paths_.size();
    clamp_focused_index();
    update_focus_visual();
}

void MindMapView::focus_prev() {
    if (ordered_paths_.isEmpty()) return;
    focused_index_--;
    if (focused_index_ < 0) focused_index_ = ordered_paths_.size() - 1;
    clamp_focused_index();
    update_focus_visual();
}

void MindMapView::focus_up() {
    if (ordered_paths_.isEmpty() || focused_index_ < 0) return;
    // Move up one row within the same column
    QString current = ordered_paths_[focused_index_];
    if (!grid_positions_.contains(current)) return;
    auto pos = grid_positions_[current];
    int target_row = pos.first - 1;
    int target_col = pos.second;

    // Find the button at (target_row, target_col)
    for (int i = 0; i < ordered_paths_.size(); ++i) {
        if (grid_positions_.contains(ordered_paths_[i])) {
            auto p = grid_positions_[ordered_paths_[i]];
            if (p.first == target_row && p.second == target_col) {
                focused_index_ = i;
                update_focus_visual();
                return;
            }
        }
    }
    // If no button above, wrap to bottom of previous column
    focus_prev();
    clamp_focused_index();
}

void MindMapView::focus_down() {
    if (ordered_paths_.isEmpty() || focused_index_ < 0) return;
    QString current = ordered_paths_[focused_index_];
    if (!grid_positions_.contains(current)) return;
    auto pos = grid_positions_[current];
    int target_row = pos.first + 1;
    int target_col = pos.second;

    for (int i = 0; i < ordered_paths_.size(); ++i) {
        if (grid_positions_.contains(ordered_paths_[i])) {
            auto p = grid_positions_[ordered_paths_[i]];
            if (p.first == target_row && p.second == target_col) {
                focused_index_ = i;
                update_focus_visual();
                return;
            }
        }
    }
    // If no button below, wrap to next
    focus_next();
    clamp_focused_index();
}

void MindMapView::activate_focused() {
    if (focused_index_ >= 0 && focused_index_ < ordered_paths_.size()) {
        emit folder_clicked(ordered_paths_[focused_index_]);
    }
}

QString MindMapView::focused_folder_path() const {
    if (focused_index_ >= 0 && focused_index_ < ordered_paths_.size()) {
        return ordered_paths_[focused_index_];
    }
    return {};
}
