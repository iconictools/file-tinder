#include "AdvancedFileTinderDialog.hpp"
#include "FolderTreeModel.hpp"
#include "MindMapView.hpp"
#include "FilterWidget.hpp"
#include "DatabaseManager.hpp"
#include "FileTinderExecutor.hpp"
#include "ui_constants.hpp"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QFileDialog>
#include <QListView>
#include <QTreeView>
#include <QMessageBox>
#include <QProgressDialog>
#include <QGroupBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QDir>
#include <QMimeDatabase>
#include <QSettings>
#include <QScreen>
#include <QDesktopServices>
#include <QUrl>
#include <QImageReader>
#include <algorithm>

AdvancedFileTinderDialog::AdvancedFileTinderDialog(const QString& source_folder,
                                                   DatabaseManager& db,
                                                   QWidget* parent)
    : StandaloneFileTinderDialog(source_folder, db, parent)
    , scroll_area_(nullptr)
    , main_content_(nullptr)
    , mind_map_view_(nullptr)
    , file_info_panel_(nullptr)
    , adv_file_icon_label_(nullptr)
    , file_name_label_(nullptr)
    , file_details_label_(nullptr)
    , adv_preview_label_(nullptr)
    , quick_access_panel_(nullptr)
    , quick_access_list_(nullptr)
    , filter_widget_(nullptr)
    , folder_model_(nullptr) {
    
    setWindowTitle(QString("File Tinder - Advanced Mode — %1").arg(QFileInfo(source_folder).fileName()));
    setMinimumWidth(ui::scaling::scaled(ui::dimensions::kAdvancedFileTinderMinWidth));
}

AdvancedFileTinderDialog::~AdvancedFileTinderDialog() = default;

void AdvancedFileTinderDialog::initialize() {
    StandaloneFileTinderDialog::initialize();
    
    // Override the base class sizing for advanced mode (needs more width)
    if (auto* screen = QApplication::primaryScreen()) {
        QRect avail = screen->availableGeometry();
        int w = qMin(ui::scaling::scaled(ui::dimensions::kAdvancedFileTinderMinWidth), avail.width() * 9 / 10);
        int h = qMin(ui::scaling::scaled(ui::dimensions::kAdvancedFileTinderMinHeight), avail.height() * 8 / 10);
        resize(w, h);
    }
}

void AdvancedFileTinderDialog::setup_ui() {
    // Clear existing layout if any
    if (layout()) {
        QLayoutItem* item;
        while ((item = layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete layout();
    }
    
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(10, 10, 10, 10);
    main_layout->setSpacing(8);
    
    // Title bar with mode switch in upper right
    auto* title_bar = new QWidget();
    auto* title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(0, 0, 0, 0);
    
    auto* title_label = new QLabel("File Tinder - Advanced Mode");
    title_label->setStyleSheet("font-size: 16px; font-weight: bold;");
    title_layout->addWidget(title_label);
    
    title_layout->addStretch();
    
    switch_mode_btn_ = new QPushButton("Switch Mode");
    switch_mode_btn_->setStyleSheet(
        "QPushButton { padding: 5px 15px; background-color: #3498db; "
        "border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #2980b9; }"
    );
    connect(switch_mode_btn_, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        auto* basic_action = menu.addAction("Basic Mode");
        auto* ai_action = menu.addAction("AI Mode");
        QAction* selected = menu.exec(switch_mode_btn_->mapToGlobal(
            QPoint(0, switch_mode_btn_->height())));
        if (selected == basic_action) {
            save_folder_tree();
            save_quick_access();
            emit switch_to_basic_mode();
        } else if (selected == ai_action) {
            save_folder_tree();
            save_quick_access();
            emit switch_to_ai_mode();
        }
    });
    title_layout->addWidget(switch_mode_btn_);
    
    main_layout->addWidget(title_bar);
    
    // Filter bar (same as Basic Mode)
    setup_filter_bar();
    
    // Mind Map (main focus - no file preview)
    setup_mind_map();
    
    // File info panel below mind map
    setup_file_info_panel();
    
    // Quick Access panel (manual add/remove, 1-0 shortcuts)
    setup_quick_access_panel();
    
    // Action buttons at bottom
    setup_action_buttons();
    
    // Initialize folder model
    folder_model_ = new FolderTreeModel(this);
    folder_model_->set_root_folder(source_folder_);
    mind_map_view_->set_model(folder_model_);
    
    // Load saved data
    load_folder_tree();
    load_quick_access();
    
    // Exclude folders in grid/quick access from processing
    QSet<QString> excluded = get_excluded_folder_paths();
    if (!excluded.isEmpty()) {
        files_.erase(std::remove_if(files_.begin(), files_.end(),
            [&excluded](const FileToProcess& f) {
                return f.is_directory && excluded.contains(f.path);
            }), files_.end());
        rebuild_filtered_indices();
    }
    
    // Requirement 2: Detect missing folders (deleted from disk between sessions)
    check_missing_folders();
    
    // Requirement 1: First-time onboarding prompt
    if (folder_model_ && folder_model_->root_node() && 
        folder_model_->root_node()->children.empty()) {
        QMessageBox::information(this, "Welcome to Advanced Mode",
            "Welcome to Advanced Mode!\n\n"
            "• Click the [+ Add Folder] button to add destination folders\n"
            "• Click any folder to assign the current file to it\n"
            "• Use keys 1-0 for Quick Access slots\n"
            "• Right-click folders for more options\n\n"
            "Tip: You can add folders outside the source directory — "
            "they'll be shown in purple.");
    }
    
    // Initialize display
    if (!files_.empty()) {
        show_current_file();
    }
}

void AdvancedFileTinderDialog::setup_filter_bar() {
    filter_widget_ = new FilterWidget(this);
    connect(filter_widget_, &FilterWidget::filter_changed, this, &AdvancedFileTinderDialog::on_filter_changed);
    connect(filter_widget_, &FilterWidget::sort_changed, this, &AdvancedFileTinderDialog::on_sort_changed);
    connect(filter_widget_, &FilterWidget::include_folders_changed, this, [this](bool include) {
        include_folders_ = include;
        // Reset counts before re-scanning and reloading state
        keep_count_ = 0;
        delete_count_ = 0;
        sort_later_count_ = 0;
        move_count_ = 0;
        scan_files();
        apply_sort();
        rebuild_filtered_indices();
        load_session_state();
        if (!filtered_indices_.empty()) {
            show_current_file();
        }
    });
    static_cast<QVBoxLayout*>(layout())->addWidget(filter_widget_);
}

void AdvancedFileTinderDialog::setup_mind_map() {
    auto* map_group = new QGroupBox("Destination Folders");
    map_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* map_layout = new QVBoxLayout(map_group);
    map_layout->setContentsMargins(5, 15, 5, 5);
    
    // Grid toolbar: rows spinner + save/load/reset
    auto* grid_toolbar = new QHBoxLayout();
    grid_toolbar->setSpacing(6);
    
    grid_toolbar->addWidget(new QLabel("Rows:"));
    auto* rows_spin = new QSpinBox();
    rows_spin->setRange(1, 20);
    rows_spin->setValue(6);
    rows_spin->setMaximumWidth(50);
    rows_spin->setToolTip("Max items per column before wrapping");
    connect(rows_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (mind_map_view_) {
            mind_map_view_->set_max_rows_per_col(val);
            mind_map_view_->refresh_layout();
        }
    });
    grid_toolbar->addWidget(rows_spin);
    
    auto* compact_btn = new QPushButton("Compact");
    compact_btn->setMaximumWidth(70);
    compact_btn->setCheckable(true);
    compact_btn->setChecked(true);
    compact_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    compact_btn->setToolTip("Toggle compact/expanded folder buttons");
    connect(compact_btn, &QPushButton::toggled, this, [this, compact_btn](bool checked) {
        compact_btn->setText(checked ? "Compact" : "Expanded");
        if (mind_map_view_) {
            mind_map_view_->set_compact_mode(checked);
            mind_map_view_->refresh_layout();
        }
    });
    grid_toolbar->addWidget(compact_btn);
    
    auto* fullpath_btn = new QPushButton("Off");
    fullpath_btn->setMaximumWidth(75);
    fullpath_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    fullpath_btn->setToolTip("Cycle path display: Off → Paths → Full Paths");
    fullpath_btn->setProperty("path_mode", 0);
    connect(fullpath_btn, &QPushButton::clicked, this, [this, fullpath_btn]() {
        int mode = fullpath_btn->property("path_mode").toInt();
        mode = (mode + 1) % 3;
        fullpath_btn->setProperty("path_mode", mode);
        static const char* labels[] = {"Off", "Paths", "Full Paths"};
        fullpath_btn->setText(labels[mode]);
        if (mind_map_view_) {
            mind_map_view_->set_path_display_mode(mode);
            mind_map_view_->refresh_layout();
        }
    });
    grid_toolbar->addWidget(fullpath_btn);

    grid_toolbar->addWidget(new QLabel("Width:"));
    auto* width_spin = new QSpinBox();
    width_spin->setRange(80, 300);
    width_spin->setValue(120);
    width_spin->setSingleStep(10);
    width_spin->setMaximumWidth(60);
    width_spin->setToolTip("Custom folder button width in pixels");
    connect(width_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (mind_map_view_) {
            mind_map_view_->set_custom_width(val);
            mind_map_view_->refresh_layout();
        }
    });
    grid_toolbar->addWidget(width_spin);
    
    // Grid sorting buttons
    auto* sort_az_btn = new QPushButton("A-Z");
    sort_az_btn->setMaximumWidth(35);
    sort_az_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 4px; }");
    sort_az_btn->setToolTip("Sort grid folders alphabetically");
    connect(sort_az_btn, &QPushButton::clicked, this, [this]() {
        if (mind_map_view_) mind_map_view_->sort_alphabetically();
    });
    grid_toolbar->addWidget(sort_az_btn);
    
    auto* sort_count_btn = new QPushButton("#");
    sort_count_btn->setMaximumWidth(25);
    sort_count_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 4px; }");
    sort_count_btn->setToolTip("Sort grid folders by file count (most files first)");
    connect(sort_count_btn, &QPushButton::clicked, this, [this]() {
        if (mind_map_view_) mind_map_view_->sort_by_count();
    });
    grid_toolbar->addWidget(sort_count_btn);
    
    grid_toolbar->addStretch();
    
    auto* save_cfg_btn = new QPushButton("Save Grid");
    save_cfg_btn->setMaximumWidth(70);
    save_cfg_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    connect(save_cfg_btn, &QPushButton::clicked, this, &AdvancedFileTinderDialog::save_grid_config);
    grid_toolbar->addWidget(save_cfg_btn);
    
    auto* load_cfg_btn = new QPushButton("Load Grid");
    load_cfg_btn->setMaximumWidth(70);
    load_cfg_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    connect(load_cfg_btn, &QPushButton::clicked, this, &AdvancedFileTinderDialog::load_grid_config);
    grid_toolbar->addWidget(load_cfg_btn);
    
    auto* reset_grid_btn = new QPushButton("Reset Grid");
    reset_grid_btn->setMaximumWidth(70);
    reset_grid_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; color: #e74c3c; }");
    connect(reset_grid_btn, &QPushButton::clicked, this, &AdvancedFileTinderDialog::reset_grid);
    grid_toolbar->addWidget(reset_grid_btn);
    
    auto* presets_btn = new QPushButton("Presets");
    presets_btn->setMaximumWidth(55);
    presets_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; color: #f39c12; }");
    presets_btn->setToolTip("Load a template grid preset");
    connect(presets_btn, &QPushButton::clicked, this, [this, presets_btn]() {
        QMenu menu;
        auto add_preset = [&](const QString& name, const QStringList& folders) {
            menu.addAction(name, [this, name, folders]() {
                auto reply = QMessageBox::question(this, "Load Preset",
                    QString("Load the '%1' template?\nThis will replace the current grid folders.").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (reply != QMessageBox::Yes) return;
                if (folder_model_) {
                    QStringList current = folder_model_->get_all_folder_paths();
                    folder_model_->blockSignals(true);
                    for (int i = current.size() - 1; i >= 0; --i)
                        folder_model_->remove_folder(current[i]);
                    for (const QString& f : folders)
                        folder_model_->add_folder(source_folder_ + "/" + f, true);
                    folder_model_->blockSignals(false);
                    emit folder_model_->folder_structure_changed();
                }
                if (mind_map_view_) mind_map_view_->refresh_layout();
            });
        };
        add_preset("Downloads Cleanup", {"Documents", "Images", "Videos", "Music",
            "Archives", "Installers", "Other"});
        add_preset("Photo Organization", {"Portraits", "Landscapes", "Events",
            "Screenshots", "Edits", "Raw Files", "Exports"});
        add_preset("Development Project", {"Source", "Assets", "Build", "Docs",
            "Config", "Tests", "Scripts", "Dependencies"});
        add_preset("Music Library", {"Albums", "Singles", "Podcasts", "Sound Effects",
            "Samples", "Stems", "Mixes"});
        add_preset("Document Archive", {"Financial", "Legal", "Personal", "Work",
            "Medical", "Education", "Reference"});
        menu.exec(presets_btn->mapToGlobal(QPoint(0, presets_btn->height())));
    });
    grid_toolbar->addWidget(presets_btn);
    
    map_layout->addLayout(grid_toolbar);
    
    mind_map_view_ = new MindMapView();
    mind_map_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    connect(mind_map_view_, &MindMapView::folder_clicked, 
            this, &AdvancedFileTinderDialog::on_folder_clicked);
    connect(mind_map_view_, &MindMapView::folder_context_menu,
            this, &AdvancedFileTinderDialog::on_folder_context_menu);
    connect(mind_map_view_, &MindMapView::add_folder_requested,
            this, &AdvancedFileTinderDialog::on_add_node_clicked);
    
    map_layout->addWidget(mind_map_view_);
    
    // Hint label
    auto* hint = new QLabel("Click folder to assign file. [+] to add. Right-click for options. K=Keep, D=Delete, S=Sort Later, 1-0=Quick Access, Tab=Grid navigation");
    hint->setStyleSheet("color: #666; font-size: 10px;");
    hint->setWordWrap(true);
    map_layout->addWidget(hint);
    
    static_cast<QVBoxLayout*>(layout())->addWidget(map_group, 1);
}

void AdvancedFileTinderDialog::setup_file_info_panel() {
    file_info_panel_ = new QWidget();
    file_info_panel_->setStyleSheet("background-color: #34495e; border-radius: 4px; padding: 8px;");
    auto* info_layout = new QHBoxLayout(file_info_panel_);
    info_layout->setContentsMargins(10, 8, 10, 8);
    
    // File type icon
    adv_file_icon_label_ = new QLabel("[FILE]");
    adv_file_icon_label_->setStyleSheet("font-size: 18px; font-weight: bold; color: #3498db; min-width: 60px;");
    adv_file_icon_label_->setAlignment(Qt::AlignCenter);
    info_layout->addWidget(adv_file_icon_label_);
    
    // File name and details
    auto* text_widget = new QWidget();
    auto* text_layout = new QVBoxLayout(text_widget);
    text_layout->setContentsMargins(0, 0, 0, 0);
    text_layout->setSpacing(2);
    
    file_name_label_ = new QLabel("No file selected");
    file_name_label_->setStyleSheet("font-size: 14px; font-weight: bold; color: #ecf0f1;");
    file_name_label_->setCursor(Qt::PointingHandCursor);
    file_name_label_->setToolTip("Double-click to open file. Right-click for options.");
    file_name_label_->installEventFilter(this);
    text_layout->addWidget(file_name_label_);
    
    file_details_label_ = new QLabel("");
    file_details_label_->setStyleSheet("font-size: 11px; color: #bdc3c7;");
    text_layout->addWidget(file_details_label_);
    
    info_layout->addWidget(text_widget, 1);
    
    // Small inline image preview (80x80)
    adv_preview_label_ = new QLabel();
    adv_preview_label_->setFixedSize(ui::scaling::scaled(80), ui::scaling::scaled(80));
    adv_preview_label_->setAlignment(Qt::AlignCenter);
    adv_preview_label_->setStyleSheet("background-color: #2c3e50; border-radius: 4px;");
    adv_preview_label_->setVisible(false);
    info_layout->addWidget(adv_preview_label_);
    
    // Progress — circular style for advanced/AI modes
    progress_bar_ = new QProgressBar();
    progress_bar_->setMaximumWidth(150);
    progress_bar_->setMaximumHeight(20);
    progress_bar_->setTextVisible(true);
    progress_bar_->setStyleSheet(
        "QProgressBar { border: 2px solid #34495e; border-radius: 10px; "
        "text-align: center; background: #2c3e50; color: #ecf0f1; font-size: 10px; }"
        "QProgressBar::chunk { background-color: #27ae60; border-radius: 8px; }");
    info_layout->addWidget(progress_bar_);
    
    static_cast<QVBoxLayout*>(layout())->addWidget(file_info_panel_);
}

void AdvancedFileTinderDialog::setup_quick_access_panel() {
    quick_access_panel_ = new QWidget();
    auto* qa_layout = new QHBoxLayout(quick_access_panel_);
    qa_layout->setContentsMargins(0, 0, 0, 0);
    
    auto* qa_label = new QLabel("Quick Access (1-0):");
    qa_label->setStyleSheet("font-weight: bold; color: #ecf0f1;");
    qa_layout->addWidget(qa_label);
    
    quick_access_list_ = new QListWidget();
    quick_access_list_->setFlow(QListView::LeftToRight);
    quick_access_list_->setMaximumHeight(40);
    quick_access_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    quick_access_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quick_access_list_->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 4px 10px; background: #34495e; border-radius: 3px; margin-right: 4px; color: #ecf0f1; }"
        "QListWidget::item:hover { background: #3d566e; }"
        "QListWidget::item:selected { background: #0078d4; color: white; }"
    );
    connect(quick_access_list_, &QListWidget::itemClicked,
            this, &AdvancedFileTinderDialog::on_quick_access_clicked);
    // Middle-click to remove items from Quick Access
    quick_access_list_->setToolTip("Click to assign file. Middle-click to remove.");
    quick_access_list_->viewport()->installEventFilter(this);
    qa_layout->addWidget(quick_access_list_, 1);
    
    auto* add_qa_btn = new QPushButton("+");
    add_qa_btn->setMaximumWidth(30);
    add_qa_btn->setToolTip("Add folder to Quick Access");
    connect(add_qa_btn, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_add_quick_access);
    qa_layout->addWidget(add_qa_btn);
    
    auto* remove_qa_btn = new QPushButton("-");
    remove_qa_btn->setMaximumWidth(30);
    remove_qa_btn->setToolTip("Remove selected from Quick Access");
    connect(remove_qa_btn, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_remove_quick_access);
    qa_layout->addWidget(remove_qa_btn);
    
    auto* clear_qa_btn = new QPushButton("Clear");
    clear_qa_btn->setMaximumWidth(40);
    clear_qa_btn->setToolTip("Clear all Quick Access slots");
    clear_qa_btn->setStyleSheet("QPushButton { font-size: 10px; color: #e74c3c; }");
    connect(clear_qa_btn, &QPushButton::clicked, this, [this]() {
        if (quick_access_folders_.isEmpty()) return;
        auto reply = QMessageBox::question(this, "Clear Quick Access",
            "Remove all Quick Access shortcuts?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            quick_access_folders_.clear();
            update_quick_access_display();
        }
    });
    qa_layout->addWidget(clear_qa_btn);
    
    static_cast<QVBoxLayout*>(layout())->addWidget(quick_access_panel_);
}

void AdvancedFileTinderDialog::setup_action_buttons() {
    auto* action_widget = new QWidget();
    auto* action_layout = new QHBoxLayout(action_widget);
    action_layout->setSpacing(6);
    
    int btn_h = ui::scaling::scaled(ui::dimensions::kThinButtonHeight);
    
    // Delete stays prominent
    delete_btn_ = new QPushButton("Delete [←]");
    delete_btn_->setMinimumHeight(btn_h);
    delete_btn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #c0392b; }"
        "QPushButton:disabled { background-color: #5d3a37; color: #888; }"
    ).arg(ui::colors::kDeleteColor));
    connect(delete_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_delete);
    action_layout->addWidget(delete_btn_, 2);
    
    // Keep compact (secondary in Advanced — clicking folders is primary)
    keep_btn_ = new QPushButton("Keep [K]");
    keep_btn_->setMinimumHeight(btn_h);
    keep_btn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; font-size: 11px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #27ae60; }"
        "QPushButton:disabled { background-color: #2d5d3a; color: #888; }"
    ).arg(ui::colors::kKeepColor));
    connect(keep_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_keep);
    action_layout->addWidget(keep_btn_, 1);
    
    // Sort Later compact
    sort_later_btn_ = new QPushButton("Sort Later [↓]");
    sort_later_btn_->setMinimumHeight(btn_h);
    sort_later_btn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; font-size: 11px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e67e22; }"
        "QPushButton:disabled { background-color: #5d4e37; color: #888; }"
    ).arg(ui::colors::kSortLaterColor));
    connect(sort_later_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_sort_later);
    action_layout->addWidget(sort_later_btn_, 1);
    
    // Undo compact
    undo_btn_ = new QPushButton("Undo [Z]");
    undo_btn_->setMinimumHeight(btn_h);
    undo_btn_->setStyleSheet(
        "QPushButton { background-color: #9b59b6; color: white; font-size: 11px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #8e44ad; }"
        "QPushButton:disabled { background-color: #5d4e6e; color: #888; }"
    );
    undo_btn_->setEnabled(false);
    connect(undo_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_undo);
    action_layout->addWidget(undo_btn_, 1);
    
    // Redo compact
    redo_btn_ = new QPushButton("Redo [Y]");
    redo_btn_->setMinimumHeight(btn_h);
    redo_btn_->setStyleSheet(
        "QPushButton { background-color: #9b59b6; color: white; font-size: 11px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #8e44ad; }"
        "QPushButton:disabled { background-color: #5d4e6e; color: #888; }"
    );
    redo_btn_->setEnabled(false);
    connect(redo_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_redo);
    action_layout->addWidget(redo_btn_, 1);
    
    static_cast<QVBoxLayout*>(layout())->addWidget(action_widget);
    
    // Bottom bar
    auto* bottom_widget = new QWidget();
    auto* bottom_layout = new QHBoxLayout(bottom_widget);
    
    auto* cancel_btn = new QPushButton("Cancel");
    cancel_btn->setStyleSheet("QPushButton { padding: 8px 16px; }");
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    bottom_layout->addWidget(cancel_btn);
    
    auto* reset_btn = new QPushButton("Reset");
    reset_btn->setStyleSheet(
        "QPushButton { padding: 8px 16px; background-color: #e74c3c; "
        "color: white; border-radius: 4px; }"
        "QPushButton:hover { background-color: #c0392b; }"
    );
    reset_btn->setToolTip("Reset all decisions and start over");
    connect(reset_btn, &QPushButton::clicked, this, [this]() { on_reset_progress(); });
    bottom_layout->addWidget(reset_btn);
    
    bottom_layout->addStretch();
    
    stats_label_ = new QLabel();
    stats_label_->setStyleSheet("color: #bdc3c7;");
    bottom_layout->addWidget(stats_label_);
    
    bottom_layout->addStretch();
    
    finish_btn_ = new QPushButton("Finish & Execute");
    finish_btn_->setStyleSheet(
        "QPushButton { padding: 10px 25px; background-color: #27ae60; "
        "color: white; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    connect(finish_btn_, &QPushButton::clicked, this, &AdvancedFileTinderDialog::on_finish);
    bottom_layout->addWidget(finish_btn_);
    
    static_cast<QVBoxLayout*>(layout())->addWidget(bottom_widget);
}

void AdvancedFileTinderDialog::on_folder_clicked(const QString& folder_path) {
    int file_idx = get_current_file_index();
    if (file_idx < 0) return;
    
    // Requirement 8: Clicking root folder = Keep in place
    if (folder_path == source_folder_) {
        on_keep();
        return;
    }
    
    auto& file = files_[file_idx];
    
    QString old_decision = file.decision;
    QString old_dest_folder = file.destination_folder;
    
    // Update counts
    if (file.decision != "pending") {
        update_decision_count(file.decision, -1);
        if (file.decision == "move" && !file.destination_folder.isEmpty() && folder_model_) {
            folder_model_->unassign_file_from_folder(file.destination_folder);
        }
    }
    
    file.decision = "move";
    file.destination_folder = folder_path;
    move_count_++;
    
    // Record for undo (store the OLD destination so it can be restored)
    record_action(file_idx, old_decision, "move", old_dest_folder);
    
    if (folder_model_) folder_model_->assign_file_to_folder(folder_path);
    if (mind_map_view_) mind_map_view_->set_selected_folder(folder_path);
    
    advance_to_next();
}

void AdvancedFileTinderDialog::on_add_node_clicked() {
    prompt_add_folder();
}

void AdvancedFileTinderDialog::prompt_add_folder() {
    QMenu menu;
    
    menu.addAction("Create New Folder...", [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Folder",
            "Enter folder name:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            QString new_path = source_folder_ + "/" + name;
            // Requirement 5: Duplicate folder prompt
            if (folder_model_ && folder_model_->find_node(new_path)) {
                QMessageBox::information(this, "Already Added",
                    QString("The folder '%1' is already in the tree.").arg(name));
                return;
            }
            if (folder_model_) folder_model_->add_folder(new_path, true);  // Virtual folder
            if (mind_map_view_) mind_map_view_->refresh_layout();
        }
    });
    
    menu.addAction("Add Existing Folder(s)...", [this]() {
        // Use non-native dialog to support multi-select
        QFileDialog dlg(this, "Select Folder(s)", source_folder_);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        dlg.setOption(QFileDialog::DontUseNativeDialog, true);
        // Enable multi-selection in the file list view
        if (auto* list_view = dlg.findChild<QListView*>("listView")) {
            list_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        }
        if (auto* tree_view = dlg.findChild<QTreeView*>()) {
            tree_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        }
        
        if (dlg.exec() == QDialog::Accepted) {
            QStringList folders = dlg.selectedFiles();
            int added = 0;
            for (const QString& folder : folders) {
                if (folder.isEmpty()) continue;
                
                // Requirement 5: Duplicate folder prompt
                if (folder_model_ && folder_model_->find_node(folder)) {
                    continue;  // Skip duplicates silently during multi-add
                }
                // Requirement 4: Warn about external folders
                bool is_external = !folder.startsWith(source_folder_);
                if (is_external && added == 0) {
                    auto reply = QMessageBox::question(this, "External Folder",
                        QString("One or more folders are outside the source directory.\n\n"
                                "Files moved there will leave the original folder's location. Continue?"),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                    if (reply != QMessageBox::Yes) return;
                }
                if (folder_model_) folder_model_->add_folder(folder, false);
                added++;
            }
            if (added > 0 && mind_map_view_) mind_map_view_->refresh_layout();
        }
    });
    
    menu.exec(QCursor::pos());
}

void AdvancedFileTinderDialog::on_folder_context_menu(const QString& folder_path, const QPoint& pos) {
    QMenu menu;
    
    // Don't offer "Move File Here" for root (Req 8 — root = keep)
    if (folder_path != source_folder_) {
        menu.addAction("Move File Here", [this, folder_path]() {
            on_folder_clicked(folder_path);
        });
        menu.addSeparator();
    }
    
    menu.addAction("Add to Quick Access", [this, folder_path]() {
        add_to_quick_access(folder_path);
    });
    
    menu.addAction("Open Folder", [folder_path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder_path));
    });
    
    // Replace with another folder
    if (folder_path != source_folder_) {
        menu.addAction("Replace with...", [this, folder_path]() {
            QString new_folder = QFileDialog::getExistingDirectory(this, "Replace with Folder", source_folder_);
            if (new_folder.isEmpty() || new_folder == folder_path) return;
            if (folder_model_ && folder_model_->find_node(new_folder)) {
                QMessageBox::information(this, "Already Added",
                    QString("'%1' is already in the tree.").arg(QFileInfo(new_folder).fileName()));
                return;
            }
            // Move assigned files from old folder to new folder
            for (auto& file : files_) {
                if (file.decision == "move" && file.destination_folder == folder_path) {
                    file.destination_folder = new_folder;
                }
            }
            // Replace in quick access too
            int qa_idx = quick_access_folders_.indexOf(folder_path);
            if (qa_idx >= 0) quick_access_folders_[qa_idx] = new_folder;
            // Remove old, add new
            if (folder_model_) {
                FolderNode* old_node = folder_model_->find_node(folder_path);
                int count = old_node ? old_node->assigned_file_count : 0;
                folder_model_->remove_folder(folder_path);
                folder_model_->add_folder(new_folder, false);
                FolderNode* new_node = folder_model_->find_node(new_folder);
                if (new_node) new_node->assigned_file_count = count;
            }
            if (mind_map_view_) mind_map_view_->refresh_layout();
            update_quick_access_display();
        });
    }
    
    menu.addAction("Add Subfolder...", [this, folder_path]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Subfolder",
            "Enter folder name:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            QString new_path = folder_path + "/" + name;
            if (folder_model_ && folder_model_->find_node(new_path)) {
                QMessageBox::information(this, "Already Added",
                    QString("The folder '%1' is already in the tree.").arg(name));
                return;
            }
            if (folder_model_) folder_model_->add_folder(new_path, true);
            if (mind_map_view_) mind_map_view_->refresh_layout();
        }
    });
    
    // Color coding
    if (folder_model_ && folder_path != source_folder_) {
        QMenu* color_menu = menu.addMenu("Set Color");
        auto add_color = [this, folder_path, color_menu](const QString& label, const QString& hex) {
            auto* action = color_menu->addAction(label);
            connect(action, &QAction::triggered, this, [this, folder_path, hex]() {
                if (folder_model_) {
                    FolderNode* node = folder_model_->find_node(folder_path);
                    if (node) {
                        node->custom_color = hex;
                        if (mind_map_view_) mind_map_view_->refresh_layout();
                    }
                }
            });
        };
        add_color("Default", "");
        add_color("Red", "#e74c3c");
        add_color("Green", "#2ecc71");
        add_color("Blue", "#3498db");
        add_color("Orange", "#f39c12");
        add_color("Purple", "#9b59b6");
        add_color("Cyan", "#1abc9c");
        add_color("Pink", "#e91e63");
    }
    
    if (folder_model_) {
        FolderNode* node = folder_model_->find_node(folder_path);
        if (node) {
            // Requirement 13: Root removal — prompt to change root and start new session
            if (node->path == source_folder_) {
                menu.addSeparator();
                menu.addAction("Change Root Folder...", [this]() {
                    auto reply = QMessageBox::question(this, "Change Root Folder",
                        "Changing the root folder will save this session and start a new one.\n\n"
                        "Do you want to proceed?",
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                    if (reply == QMessageBox::Yes) {
                        save_folder_tree();
                        save_quick_access();
                        save_session_state();
                        reject();
                    }
                });
            }
            // Requirement 14: Allow removing any non-root folder (virtual or real)
            else {
                menu.addSeparator();
                menu.addAction("Remove from Tree", [this, folder_path, node]() {
                    // Requirement 12: Warn about assigned files
                    if (node->assigned_file_count > 0) {
                        auto reply = QMessageBox::warning(this, "Folder Has Assigned Files",
                            QString("This folder has %1 file(s) assigned to it.\n\n"
                                    "Removing it will set those files back to 'pending' "
                                    "and they will appear at the bottom of the file list.\n\n"
                                    "Do you want to proceed?")
                                .arg(node->assigned_file_count),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply != QMessageBox::Yes) return;
                        
                        // Revert assigned files to pending
                        for (auto& file : files_) {
                            if (file.decision == "move" && file.destination_folder == folder_path) {
                                file.decision = "pending";
                                file.destination_folder.clear();
                                move_count_--;
                            }
                        }
                    }
                    
                    // Requirement 11: If folder is in Quick Access, ask user
                    if (quick_access_folders_.contains(folder_path)) {
                        auto reply = QMessageBox::question(this, "Also in Quick Access",
                            "This folder is also in Quick Access. Remove it from Quick Access too?",
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                        if (reply == QMessageBox::Yes) {
                            quick_access_folders_.removeAll(folder_path);
                            update_quick_access_display();
                        }
                    }
                    
                    if (folder_model_) folder_model_->remove_folder(folder_path);
                    if (mind_map_view_) mind_map_view_->refresh_layout();
                    update_stats();
                    show_current_file();
                });
            }
        }
    }
    
    menu.exec(pos);
}

void AdvancedFileTinderDialog::load_quick_access() {
    quick_access_folders_ = db_.get_quick_access_folders(source_folder_);
    update_quick_access_display();
}

void AdvancedFileTinderDialog::save_quick_access() {
    db_.save_quick_access_folders(source_folder_, quick_access_folders_);
}

void AdvancedFileTinderDialog::add_to_quick_access(const QString& folder_path) {
    if (quick_access_folders_.contains(folder_path)) return;
    if (quick_access_folders_.size() >= kMaxQuickAccess) {
        // Requirement 10: Prompt to choose one to replace
        auto reply = QMessageBox::question(this, "Quick Access Full",
            QString("Quick Access is full (%1/%2).\n\n"
                    "Do you want to choose a slot to replace?")
                .arg(quick_access_folders_.size()).arg(kMaxQuickAccess),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes) return;
        
        // Build list of current slots for user to pick from
        QStringList items;
        for (int i = 0; i < quick_access_folders_.size(); ++i) {
            items << QString("%1: %2").arg(i == 9 ? 0 : i + 1)
                     .arg(QFileInfo(quick_access_folders_[i]).fileName());
        }
        bool ok;
        QString chosen = QInputDialog::getItem(this, "Replace Quick Access Slot",
            "Select a slot to replace:", items, 0, false, &ok);
        if (!ok) return;
        
        int slot = items.indexOf(chosen);
        if (slot >= 0) {
            quick_access_folders_[slot] = folder_path;
            update_quick_access_display();
        }
        return;
    }
    quick_access_folders_.append(folder_path);
    update_quick_access_display();
}

void AdvancedFileTinderDialog::remove_from_quick_access(int index) {
    if (index >= 0 && index < quick_access_folders_.size()) {
        quick_access_folders_.removeAt(index);
        update_quick_access_display();
    }
}

void AdvancedFileTinderDialog::update_quick_access_display() {
    quick_access_list_->clear();
    
    static const int kMaxQuickAccessNameLength = 14;
    
    for (int i = 0; i < quick_access_folders_.size(); ++i) {
        QString path = quick_access_folders_[i];
        QString folder_name = QFileInfo(path).fileName();
        // Truncate long names to keep uniform item width
        if (folder_name.length() > kMaxQuickAccessNameLength) {
            folder_name = folder_name.left(kMaxQuickAccessNameLength - 1) + "…";
        }
        QString label = QString("%1: %2").arg(i == 9 ? 0 : i + 1).arg(folder_name);
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(QSize(ui::scaling::scaled(120), ui::scaling::scaled(28)));
        quick_access_list_->addItem(item);
    }
}

void AdvancedFileTinderDialog::on_quick_access_clicked(QListWidgetItem* item) {
    QString folder_path = item->data(Qt::UserRole).toString();
    if (!folder_path.isEmpty()) {
        on_folder_clicked(folder_path);
    }
}

void AdvancedFileTinderDialog::on_add_quick_access() {
    QString folder = QFileDialog::getExistingDirectory(this, "Add to Quick Access", source_folder_);
    if (!folder.isEmpty()) {
        add_to_quick_access(folder);
    }
}

void AdvancedFileTinderDialog::on_remove_quick_access() {
    int row = quick_access_list_->currentRow();
    if (row >= 0) {
        remove_from_quick_access(row);
    }
}

void AdvancedFileTinderDialog::update_file_info_display() {
    int idx = get_current_file_index();
    if (idx < 0 || idx >= static_cast<int>(files_.size())) {
        if (adv_file_icon_label_) adv_file_icon_label_->setText("[---]");
        if (file_name_label_) file_name_label_->setText("No file selected");
        if (file_details_label_) file_details_label_->setText("");
        if (adv_preview_label_) adv_preview_label_->setVisible(false);
        return;
    }
    
    const auto& file = files_[idx];
    QString path = file.path;
    QFileInfo info(path);
    
    // Icon
    if (adv_file_icon_label_) adv_file_icon_label_->setText(get_file_type_icon(path));
    
    // Name
    if (file_name_label_) file_name_label_->setText(info.fileName());
    
    // Details
    QString size_str;
    qint64 size = info.size();
    if (size < 1024LL) size_str = QString("%1 B").arg(size);
    else if (size < 1024LL*1024) size_str = QString("%1 KB").arg(size/1024.0, 0, 'f', 1);
    else if (size < 1024LL*1024*1024) size_str = QString("%1 MB").arg(size/(1024.0*1024.0), 0, 'f', 2);
    else size_str = QString("%1 GB").arg(size/(1024.0*1024.0*1024.0), 0, 'f', 2);
    
    QString details = QString("%1 | %2 | %3")
        .arg(size_str)
        .arg(info.isDir() ? "Folder" : info.suffix().toUpper())
        .arg(info.lastModified().toString("yyyy-MM-dd hh:mm"));
    if (file_details_label_) file_details_label_->setText(details);
    
    // Small inline image preview (use QImageReader for efficient loading)
    if (adv_preview_label_) {
        if (file.mime_type.startsWith("image/") && !info.isDir()) {
            int sz = ui::scaling::scaled(80);
            QImageReader reader(path);
            if (reader.canRead()) {
                QSize orig = reader.size();
                if (orig.isValid()) {
                    reader.setScaledSize(orig.scaled(sz, sz, Qt::KeepAspectRatio));
                }
                QImage img = reader.read();
                if (!img.isNull()) {
                    adv_preview_label_->setPixmap(QPixmap::fromImage(img));
                    adv_preview_label_->setVisible(true);
                } else {
                    adv_preview_label_->setVisible(false);
                }
            } else {
                adv_preview_label_->setVisible(false);
            }
        } else {
            adv_preview_label_->setVisible(false);
        }
    }
}

QString AdvancedFileTinderDialog::get_file_type_icon(const QString& path) {
    int idx = get_current_file_index();
    // Use cached mime type from files_ to avoid expensive disk lookup
    QString mime;
    if (idx >= 0 && idx < static_cast<int>(files_.size()) &&
        QFileInfo(files_[idx].path).canonicalFilePath() == QFileInfo(path).canonicalFilePath()) {
        mime = files_[idx].mime_type;
    }
    
    QFileInfo info(path);
    if (info.isDir()) return "[DIR]";
    
    if (mime.isEmpty()) {
        QMimeDatabase db;
        mime = db.mimeTypeForFile(path).name();
    }
    
    if (mime.startsWith("image/")) return "[IMG]";
    if (mime.startsWith("video/")) return "[VID]";
    if (mime.startsWith("audio/")) return "[AUD]";
    if (mime.contains("pdf") || mime.contains("document") || mime.contains("text")) return "[DOC]";
    if (mime.contains("zip") || mime.contains("archive") || mime.contains("compressed")) return "[ZIP]";
    return "[FILE]";
}

void AdvancedFileTinderDialog::show_current_file() {
    update_file_info_display();
    update_stats();
    
    // Update progress bar with filtered items
    if (progress_bar_) {
        int filtered_total = static_cast<int>(filtered_indices_.size());
        int filtered_reviewed = 0;
        for (int idx : filtered_indices_) {
            if (files_[idx].decision != "pending") {
                filtered_reviewed++;
            }
        }
        progress_bar_->setMaximum(filtered_total);
        progress_bar_->setValue(filtered_reviewed);
        progress_bar_->setFormat(QString("%1 / %2 assigned").arg(filtered_reviewed).arg(filtered_total));
    }
    
    // Requirement 16: Highlight the assigned folder when showing a file
    if (mind_map_view_) {
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            const auto& file = files_[file_idx];
            if (file.decision == "move" && !file.destination_folder.isEmpty()) {
                mind_map_view_->set_selected_folder(file.destination_folder);
            } else {
                mind_map_view_->set_selected_folder(QString());
            }
        } else {
            mind_map_view_->set_selected_folder(QString());
        }
    }
}

void AdvancedFileTinderDialog::closeEvent(QCloseEvent* event) {
    // Route through reject() — don't delegate to QDialog::closeEvent
    event->ignore();
    reject();
}

void AdvancedFileTinderDialog::reject() {
    // Save advanced-mode-specific state before base class handles close prompt
    save_folder_tree();
    save_quick_access();
    
    // Base class reject() shows save prompt and terminates exec()
    StandaloneFileTinderDialog::reject();
}

bool AdvancedFileTinderDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == file_name_label_ && event->type() == QEvent::MouseButtonDblClick) {
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(files_[file_idx].path));
        }
        return true;
    }
    if (obj == file_name_label_ && event->type() == QEvent::ContextMenu) {
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            QMenu menu;
            QString folder = QFileInfo(files_[file_idx].path).absolutePath();
            menu.addAction("Open Containing Folder", [folder]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            });
            menu.addAction("Open File", [this, file_idx]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(files_[file_idx].path));
            });
            menu.exec(QCursor::pos());
        }
        return true;
    }
    // Middle-click to remove quick access items
    if (quick_access_list_ && obj == quick_access_list_->viewport() &&
        event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            auto* item = quick_access_list_->itemAt(me->pos());
            if (item) {
                int row = quick_access_list_->row(item);
                remove_from_quick_access(row);
                return true;
            }
        }
    }
    return StandaloneFileTinderDialog::eventFilter(obj, event);
}

void AdvancedFileTinderDialog::on_finish() {
    save_folder_tree();
    save_quick_access();
    show_review_summary();
}

void AdvancedFileTinderDialog::on_undo() {
    if (undo_stack_.empty()) return;
    
    // Peek at the action we're about to undo to handle folder model
    const ActionRecord& last_action = undo_stack_.back();
    
    // If we're undoing a "move", unassign from the destination folder in the model
    if (last_action.new_decision == "move" && folder_model_) {
        int file_idx = last_action.file_index;
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            const auto& file = files_[file_idx];
            if (!file.destination_folder.isEmpty()) {
                folder_model_->unassign_file_from_folder(file.destination_folder);
            }
        }
    }
    
    // Call base implementation to do the actual undo
    StandaloneFileTinderDialog::on_undo();
    
    // Requirement 15: Visual deselect after undo
    if (mind_map_view_) {
        // If the current file has a folder assignment, show it; otherwise clear selection
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            const auto& file = files_[file_idx];
            if (file.decision == "move" && !file.destination_folder.isEmpty()) {
                mind_map_view_->set_selected_folder(file.destination_folder);
            } else {
                mind_map_view_->set_selected_folder(QString());  // Clear selection
            }
        } else {
            mind_map_view_->set_selected_folder(QString());
        }
    }
}

void AdvancedFileTinderDialog::keyPressEvent(QKeyEvent* event) {
    // If in grid keyboard navigation mode, handle grid navigation keys
    if (mind_map_view_ && mind_map_view_->keyboard_mode()) {
        switch (event->key()) {
            case Qt::Key_Tab:
            case Qt::Key_Right:
                mind_map_view_->focus_next();
                return;
            case Qt::Key_Backtab:  // Shift+Tab
            case Qt::Key_Left:
                mind_map_view_->focus_prev();
                return;
            case Qt::Key_Up:
                mind_map_view_->focus_up();
                return;
            case Qt::Key_Down:
                mind_map_view_->focus_down();
                return;
            case Qt::Key_Space:
            case Qt::Key_Return:
            case Qt::Key_Enter:
                mind_map_view_->activate_focused();
                return;
            case Qt::Key_Escape:
                // Exit keyboard grid navigation mode
                mind_map_view_->set_keyboard_mode(false);
                return;
            default:
                break;
        }
    }

    // Tab key enters grid keyboard navigation mode
    if (event->key() == Qt::Key_Tab && mind_map_view_ && !mind_map_view_->keyboard_mode()) {
        mind_map_view_->set_keyboard_mode(true);
        return;
    }

    // Number keys 1-9, 0 for quick access
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        int index = event->key() - Qt::Key_1;
        if (index < quick_access_folders_.size()) {
            on_folder_clicked(quick_access_folders_[index]);
            return;
        }
    }
    if (event->key() == Qt::Key_0) {
        if (quick_access_folders_.size() >= 10) {
            on_folder_clicked(quick_access_folders_[9]);
            return;
        }
    }
    
    // Arrow keys and letter shortcuts
    switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_D:
            on_delete();
            break;
        case Qt::Key_Right:
            // In Advanced Mode, right doesn't auto-keep, need to select folder
            break;
        case Qt::Key_K:
            on_keep();
            break;
        case Qt::Key_Z:
            on_undo();
            break;
        case Qt::Key_Down:
        case Qt::Key_S:
            on_sort_later();
            break;
        case Qt::Key_Up:
            break;  // No back — use Z for Undo
        case Qt::Key_N:
            prompt_add_folder();
            break;
        default:
            StandaloneFileTinderDialog::keyPressEvent(event);
    }
}

void AdvancedFileTinderDialog::on_filter_changed() {
    if (!filter_widget_) return;
    
    FileFilterType filter_type = filter_widget_->get_filter_type();
    
    // Prompt user about resetting progress when filter changes
    int reviewed = keep_count_ + delete_count_ + sort_later_count_ + move_count_;
    if (reviewed > 0) {
        auto reply = QMessageBox::question(this, "Filter Changed",
            QString("You have %1 decisions made. Do you want to reset progress?\n\n"
                    "• Yes — clear all decisions\n"
                    "• No — keep existing decisions").arg(reviewed),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            for (auto& file : files_) {
                if (file.decision == "move" && !file.destination_folder.isEmpty() && folder_model_) {
                    folder_model_->unassign_file_from_folder(file.destination_folder);
                }
                file.decision = "pending";
                file.destination_folder.clear();
            }
            keep_count_ = 0;
            delete_count_ = 0;
            sort_later_count_ = 0;
            move_count_ = 0;
            undo_stack_.clear();
            redo_stack_.clear();
            if (undo_btn_) undo_btn_->setEnabled(false);
            if (redo_btn_) redo_btn_->setEnabled(false);
            db_.clear_session(source_folder_);
        }
    }
    
    current_filter_ = filter_type;
    
    if (filter_type == FileFilterType::Custom) {
        custom_extensions_.clear();
        for (const QString& ext : filter_widget_->get_custom_extensions()) {
            custom_extensions_.append("." + ext);
        }
    }
    
    include_folders_ = filter_widget_->get_include_folders();
    
    rebuild_filtered_indices();
    
    // Find first pending file
    current_filtered_index_ = 0;
    for (size_t i = 0; i < filtered_indices_.size(); ++i) {
        if (files_[filtered_indices_[i]].decision == "pending") {
            current_filtered_index_ = static_cast<int>(i);
            break;
        }
    }
    
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    update_stats();
}

void AdvancedFileTinderDialog::on_sort_changed() {
    if (!filter_widget_) return;
    
    SortField sf = filter_widget_->get_sort_field();
    switch (sf) {
        case SortField::Name: sort_field_ = FileSortField::Name; break;
        case SortField::Size: sort_field_ = FileSortField::Size; break;
        case SortField::Type: sort_field_ = FileSortField::Type; break;
        case SortField::DateModified: sort_field_ = FileSortField::DateModified; break;
    }
    
    sort_order_ = filter_widget_->get_sort_order();
    
    apply_sort();
    rebuild_filtered_indices();
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
}

void AdvancedFileTinderDialog::load_folder_tree() {
    if (folder_model_) {
        // Block signals during bulk load to prevent O(N) refresh_layout() calls
        // (each add_folder() emits folder_structure_changed → refresh_layout)
        folder_model_->blockSignals(true);
        folder_model_->load_from_database(db_, source_folder_);
        folder_model_->blockSignals(false);
    }
    if (mind_map_view_) {
        mind_map_view_->refresh_layout();
    }
}

void AdvancedFileTinderDialog::save_grid_config() {
    if (!folder_model_ || !folder_model_->root_node()) return;
    
    bool ok;
    QString name = QInputDialog::getText(this, "Save Grid Configuration",
        "Enter a name for this grid layout:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    
    QStringList paths = folder_model_->get_all_folder_paths();
    
    // Append display settings as metadata entries (prefixed with __meta__)
    bool compact = mind_map_view_ ? mind_map_view_->compact_mode() : true;
    int rows = mind_map_view_ ? mind_map_view_->max_rows_per_col() : 6;
    int custom_w = mind_map_view_ ? mind_map_view_->custom_width() : 0;
    int path_mode = mind_map_view_ ? mind_map_view_->path_display_mode() : 0;
    paths.append(QString("__meta__compact=%1").arg(compact ? 1 : 0));
    paths.append(QString("__meta__rows=%1").arg(rows));
    paths.append(QString("__meta__width=%1").arg(custom_w));
    paths.append(QString("__meta__fullpaths=%1").arg(path_mode));
    
    db_.save_grid_config(source_folder_, name, paths);
    QMessageBox::information(this, "Saved", QString("Grid configuration '%1' saved with %2 folder(s).")
        .arg(name).arg(paths.size()));
}

void AdvancedFileTinderDialog::load_grid_config() {
    QStringList names = db_.get_grid_config_names(source_folder_);
    if (names.isEmpty()) {
        QMessageBox::information(this, "No Configs", "No saved grid configurations for this folder.");
        return;
    }
    
    bool ok;
    QString name = QInputDialog::getItem(this, "Load Grid Configuration",
        "Select a configuration:", names, 0, false, &ok);
    if (!ok) return;
    
    QStringList paths = db_.get_grid_config(source_folder_, name);
    if (paths.isEmpty()) return;
    
    // Extract display metadata from config
    bool has_meta = false;
    bool meta_compact = true;
    int meta_rows = 6;
    int meta_width = 0;
    int meta_fullpaths = 0;
    QStringList folder_paths;
    for (const QString& p : paths) {
        if (p.startsWith("__meta__")) {
            has_meta = true;
            if (p.startsWith("__meta__compact=")) meta_compact = p.mid(16).toInt();
            else if (p.startsWith("__meta__rows=")) meta_rows = p.mid(13).toInt();
            else if (p.startsWith("__meta__width=")) meta_width = p.mid(14).toInt();
            else if (p.startsWith("__meta__fullpaths=")) meta_fullpaths = p.mid(18).toInt();
        } else {
            folder_paths.append(p);
        }
    }
    
    // Clear current tree (except root) — remove in reverse order so
    // children are removed before their parents (avoid double-free)
    if (folder_model_) {
        QStringList current = folder_model_->get_all_folder_paths();
        folder_model_->blockSignals(true);
        for (int i = current.size() - 1; i >= 0; --i) {
            folder_model_->remove_folder(current[i]);
        }
        // Add folders from config
        for (const QString& p : folder_paths) {
            bool is_virtual = !QDir(p).exists();
            folder_model_->add_folder(p, is_virtual);
        }
        folder_model_->blockSignals(false);
    }
    // Apply display settings
    if (has_meta && mind_map_view_) {
        mind_map_view_->set_compact_mode(meta_compact);
        mind_map_view_->set_max_rows_per_col(meta_rows);
        mind_map_view_->set_custom_width(meta_width);
        mind_map_view_->set_path_display_mode(meta_fullpaths);
    }
    if (mind_map_view_) mind_map_view_->refresh_layout();
}

void AdvancedFileTinderDialog::reset_grid() {
    if (!folder_model_ || !folder_model_->root_node()) return;
    
    QStringList current = folder_model_->get_all_folder_paths();
    if (current.isEmpty()) {
        QMessageBox::information(this, "Already Empty", "The grid only contains the root folder.");
        return;
    }
    
    // Check for virtual folders
    QStringList virtual_folders = folder_model_->get_virtual_folders();
    
    QString msg = QString("Remove all %1 folder(s) from the grid, leaving only the root?").arg(current.size());
    if (!virtual_folders.isEmpty()) {
        msg += QString("\n\nThis includes %1 virtual folder(s). These will be deleted.").arg(virtual_folders.size());
    }
    
    auto reply = QMessageBox::question(this, "Reset Grid", msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    
    // Revert files assigned to any of these folders
    for (auto& file : files_) {
        if (file.decision == "move" && current.contains(file.destination_folder)) {
            file.decision = "pending";
            file.destination_folder.clear();
            move_count_--;
        }
    }
    
    folder_model_->blockSignals(true);
    for (int i = current.size() - 1; i >= 0; --i) {
        folder_model_->remove_folder(current[i]);
    }
    folder_model_->blockSignals(false);
    
    if (mind_map_view_) mind_map_view_->refresh_layout();
    update_stats();
    show_current_file();
}

QSet<QString> AdvancedFileTinderDialog::get_excluded_folder_paths() const {
    QSet<QString> excluded;
    if (folder_model_) {
        QStringList tree_paths = folder_model_->get_all_folder_paths();
        for (const QString& p : tree_paths) {
            excluded.insert(p);
        }
    }
    for (const QString& p : quick_access_folders_) {
        excluded.insert(p);
    }
    return excluded;
}

QStringList AdvancedFileTinderDialog::get_destination_folders() const {
    if (folder_model_) {
        return folder_model_->get_all_folder_paths();
    }
    return {};
}

void AdvancedFileTinderDialog::save_folder_tree() {
    if (folder_model_) {
        folder_model_->save_to_database(db_, source_folder_);
    }
}

void AdvancedFileTinderDialog::check_missing_folders() {
    if (!folder_model_ || !folder_model_->root_node()) return;
    
    // Collect non-virtual folders that no longer exist on disk
    QStringList missing;
    std::function<void(FolderNode*)> check = [&](FolderNode* node) {
        // Only check non-root, non-virtual folders
        if (node != folder_model_->root_node() && node->exists && !QDir(node->path).exists()) {
            missing.append(node->path);
        }
        for (const auto& child : node->children) {
            check(child.get());
        }
    };
    check(folder_model_->root_node());
    
    if (missing.isEmpty()) return;
    
    // Requirement 2: Warn user about missing folders
    QString msg = QString("%1 folder(s) no longer exist on disk:\n\n").arg(missing.size());
    for (const QString& path : missing) {
        msg += "• " + QFileInfo(path).fileName() + "\n";
    }
    msg += "\nDo you want to keep them as virtual folders (will be created during execution), "
           "or remove them from the tree?";
    
    auto reply = QMessageBox::question(this, "Missing Folders Detected", msg,
        QMessageBox::Yes | QMessageBox::No,  // Yes=keep as virtual, No=remove
        QMessageBox::Yes);
    
    if (reply == QMessageBox::Yes) {
        // Mark them as virtual (not existing)
        for (const QString& path : missing) {
            FolderNode* node = folder_model_->find_node(path);
            if (node) {
                node->exists = false;
            }
        }
    } else {
        // Remove them from tree, reverting any assigned files
        for (const QString& path : missing) {
            FolderNode* node = folder_model_->find_node(path);
            if (node && node->assigned_file_count > 0) {
                for (auto& file : files_) {
                    if (file.decision == "move" && file.destination_folder == path) {
                        file.decision = "pending";
                        file.destination_folder.clear();
                        move_count_--;
                    }
                }
            }
            folder_model_->remove_folder(path);
        }
    }
    
    if (mind_map_view_) mind_map_view_->refresh_layout();
}
