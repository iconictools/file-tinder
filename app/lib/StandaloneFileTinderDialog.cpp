#include "StandaloneFileTinderDialog.hpp"
#include "DatabaseManager.hpp"
#include "FileTinderExecutor.hpp"
#include "AppLogger.hpp"
#include "ImagePreviewWindow.hpp"
#include "FileListWindow.hpp"
#include "DuplicateDetectionWindow.hpp"
#include "ui_constants.hpp"
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QScreen>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QPixmap>
#include <QMimeDatabase>
#include <QTextStream>
#include <QScrollArea>
#include <QGroupBox>
#include <QProgressDialog>
#include <QInputDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QApplication>
#include <QSettings>
#include <QSplitter>
#include <QPointer>
#include <QMenu>
#include <QSet>
#include <QDesktopServices>
#include <QUrl>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QLocale>
#include <QMenu>
#include <QImageReader>
#include <algorithm>

StandaloneFileTinderDialog::StandaloneFileTinderDialog(const QString& source_folder,
                                                       DatabaseManager& db,
                                                       QWidget* parent)
    : QDialog(parent)
    , current_filtered_index_(0)
    , source_folder_(source_folder)
    , db_(db)
    , current_filter_(FileFilterType::All)
    , sort_field_(FileSortField::Name)
    , sort_order_(SortOrder::Ascending)
    , include_folders_(false)
    , keep_count_(0)
    , delete_count_(0)
    , sort_later_count_(0)
    , move_count_(0)
    , image_preview_window_(nullptr)
    , preview_label_(nullptr)
    , file_info_label_(nullptr)
    , file_icon_label_(nullptr)
    , progress_label_(nullptr)
    , stats_label_(nullptr)
    , progress_bar_(nullptr)
    , filter_combo_(nullptr)
    , sort_combo_(nullptr)
    , sort_order_btn_(nullptr)
    , folders_checkbox_(nullptr)
    , shortcuts_label_(nullptr)
    , back_btn_(nullptr)
    , delete_btn_(nullptr)
    , sort_later_btn_(nullptr)
    , keep_btn_(nullptr)
    , undo_btn_(nullptr)
    , redo_btn_(nullptr)
    , preview_btn_(nullptr)
    , finish_btn_(nullptr)
    , switch_mode_btn_(nullptr)
    , help_btn_(nullptr)
    , swipe_animation_(nullptr)
    , resize_timer_(nullptr)
    , file_position_label_(nullptr)
    , size_badge_label_(nullptr)
    , search_box_(nullptr) {
    
    setWindowTitle(QString("File Tinder - Basic Mode — %1").arg(QFileInfo(source_folder).fileName()));
    
    // Setup resize timer for debouncing preview updates
    resize_timer_ = new QTimer(this);
    resize_timer_->setSingleShot(true);
    resize_timer_->setInterval(150);  // 150ms debounce
    connect(resize_timer_, &QTimer::timeout, this, [this]() {
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            update_preview(files_[file_idx].path);
        }
    });
    
    // Don't call setup_ui() here - it's virtual and could cause issues
    // with derived classes. Call initialize() after construction instead.
}

StandaloneFileTinderDialog::~StandaloneFileTinderDialog() = default;

void StandaloneFileTinderDialog::initialize() {
    setup_ui();
    scan_files();
    apply_sort();  // Sort files after loading
    rebuild_filtered_indices();
    load_session_state();
    
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    
    // Save this folder as last used
    save_last_folder();
    
    // Start session timer for review pace tracking
    session_timer_.start();
    
    // Size window to fit within the available screen area
    if (auto* screen = QApplication::primaryScreen()) {
        QRect avail = screen->availableGeometry();
        int target_w = qMin(ui::scaling::scaled(ui::dimensions::kStandaloneFileTinderMinWidth),
                            avail.width() * 85 / 100);
        int target_h = qMin(ui::scaling::scaled(ui::dimensions::kStandaloneFileTinderMinHeight),
                            avail.height() * 75 / 100);
        resize(target_w, target_h);
    }
}

void StandaloneFileTinderDialog::save_last_folder() {
    QSettings settings("FileTinder", "FileTinder");
    settings.setValue("lastFolder", source_folder_);
}

QString StandaloneFileTinderDialog::get_last_folder() {
    QSettings settings("FileTinder", "FileTinder");
    return settings.value("lastFolder", QDir::homePath()).toString();
}

void StandaloneFileTinderDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(15, 15, 15, 15);
    main_layout->setSpacing(10);
    
    // Top bar: Title + Mode switch (upper right)
    auto* top_bar = new QWidget();
    auto* top_layout = new QHBoxLayout(top_bar);
    top_layout->setContentsMargins(0, 0, 0, 0);
    
    auto* title_label = new QLabel("File Tinder - Basic Mode");
    title_label->setStyleSheet(QString(
        "font-size: %1px; font-weight: bold; color: %2;"
    ).arg(ui::fonts::kHeaderSize).arg(ui::colors::kMoveColor));
    top_layout->addWidget(title_label);
    
    file_position_label_ = new QLabel("File 0 of 0");
    file_position_label_->setStyleSheet("color: #95a5a6; font-size: 12px;");
    top_layout->addWidget(file_position_label_);

    // Duplicate detection ! button — visible when current file has duplicates
    duplicate_btn_ = new QPushButton("!");
    duplicate_btn_->setFixedSize(ui::scaling::scaled(28), ui::scaling::scaled(28));
    duplicate_btn_->setToolTip("Duplicates detected - click to view");
    duplicate_btn_->setStyleSheet(
        "QPushButton { font-size: 14px; font-weight: bold; background-color: #e67e22; "
        "border-radius: 14px; color: white; border: 2px solid #d35400; }"
        "QPushButton:hover { background-color: #d35400; }"
    );
    duplicate_btn_->setVisible(false);
    connect(duplicate_btn_, &QPushButton::clicked, this, [this]() {
        auto* dw = new DuplicateDetectionWindow(files_, source_folder_, this);
        connect(dw, &DuplicateDetectionWindow::files_deleted, this, [this](const QList<int>& indices) {
            for (int fi : indices) {
                if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                    auto& file = files_[fi];
                    QString old_decision = file.decision;
                    file.decision = "delete";
                    update_decision_count(old_decision, -1);
                    delete_count_++;
                    record_action(fi, old_decision, "delete");
                }
            }
            update_progress();
            update_stats();
        });
        dw->exec();
    });
    top_layout->addWidget(duplicate_btn_);
    
    top_layout->addStretch();
    
    // Mode switch in upper right corner
    switch_mode_btn_ = new QPushButton("Switch Mode");
    switch_mode_btn_->setFixedSize(ui::scaling::scaled(130), ui::scaling::scaled(32));
    switch_mode_btn_->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 5px 10px; "
        "background-color: #9b59b6; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #8e44ad; }"
    );
    connect(switch_mode_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_switch_mode_clicked);
    top_layout->addWidget(switch_mode_btn_);
    
    // Help button
    help_btn_ = new QPushButton("?");
    help_btn_->setFixedSize(ui::scaling::scaled(32), ui::scaling::scaled(32));
    help_btn_->setToolTip("Keyboard Shortcuts");
    help_btn_->setStyleSheet(
        "QPushButton { font-size: 14px; font-weight: bold; background-color: #34495e; "
        "border-radius: 16px; color: white; }"
        "QPushButton:hover { background-color: #3d566e; }"
    );
    connect(help_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::show_shortcuts_help);
    top_layout->addWidget(help_btn_);
    
    main_layout->addWidget(top_bar);
    
    // Filter/Sort bar
    auto* filter_bar = new QWidget();
    auto* filter_layout = new QHBoxLayout(filter_bar);
    filter_layout->setContentsMargins(0, 5, 0, 5);
    filter_layout->setSpacing(10);
    
    // Filter
    filter_layout->addWidget(new QLabel("Filter:"));
    filter_combo_ = new QComboBox();
    filter_combo_->addItem("All Files", static_cast<int>(FileFilterType::All));
    filter_combo_->addItem("Images", static_cast<int>(FileFilterType::Images));
    filter_combo_->addItem("Videos", static_cast<int>(FileFilterType::Videos));
    filter_combo_->addItem("Audio", static_cast<int>(FileFilterType::Audio));
    filter_combo_->addItem("Documents", static_cast<int>(FileFilterType::Documents));
    filter_combo_->addItem("Archives", static_cast<int>(FileFilterType::Archives));
    filter_combo_->addItem("Other", static_cast<int>(FileFilterType::Other));
    filter_combo_->addItem("Folders Only", static_cast<int>(FileFilterType::FoldersOnly));
    filter_combo_->addItem("Specify...", static_cast<int>(FileFilterType::Custom));
    filter_combo_->setMinimumWidth(120);
    filter_combo_->setStyleSheet(
        "QComboBox { padding: 4px 8px; background-color: #34495e; "
        "border-radius: 4px; color: white; }"
        "QComboBox:hover { background-color: #3d566e; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox:focus { border: 2px solid #3498db; }"
    );
    connect(filter_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StandaloneFileTinderDialog::on_filter_changed);
    filter_layout->addWidget(filter_combo_);
    
    // Include folders checkbox
    folders_checkbox_ = new QCheckBox("Include Folders");
    folders_checkbox_->setStyleSheet("color: #bdc3c7;");
    connect(folders_checkbox_, &QCheckBox::stateChanged, 
            this, &StandaloneFileTinderDialog::on_folders_toggle_changed);
    filter_layout->addWidget(folders_checkbox_);
    
    filter_layout->addSpacing(20);
    
    // Sort
    filter_layout->addWidget(new QLabel("Sort:"));
    sort_combo_ = new QComboBox();
    sort_combo_->addItem("Name", static_cast<int>(FileSortField::Name));
    sort_combo_->addItem("Size", static_cast<int>(FileSortField::Size));
    sort_combo_->addItem("Type", static_cast<int>(FileSortField::Type));
    sort_combo_->addItem("Date Modified", static_cast<int>(FileSortField::DateModified));
    sort_combo_->setMinimumWidth(100);
    sort_combo_->setStyleSheet(
        "QComboBox { padding: 4px 8px; background-color: #34495e; "
        "border-radius: 4px; color: white; }"
        "QComboBox:hover { background-color: #3d566e; }"
        "QComboBox::drop-down { border: none; }"
    );
    connect(sort_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StandaloneFileTinderDialog::on_sort_changed);
    filter_layout->addWidget(sort_combo_);
    
    // Sort order toggle button
    sort_order_btn_ = new QPushButton("Asc");
    sort_order_btn_->setFixedSize(ui::scaling::scaled(50), ui::scaling::scaled(28));
    sort_order_btn_->setStyleSheet(
        "QPushButton { padding: 4px; background-color: #34495e; "
        "border-radius: 4px; color: white; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d566e; }"
    );
    connect(sort_order_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_sort_order_toggled);
    filter_layout->addWidget(sort_order_btn_);
    
    filter_layout->addStretch();
    main_layout->addWidget(filter_bar);
    
    // File card — unified preview + info + stats panel
    auto* file_card = new QWidget();
    file_card->setStyleSheet(
        "QWidget#fileCard { background-color: #2c3e50; border-radius: 8px; "
        "border: 1px solid #34495e; }");
    file_card->setObjectName("fileCard");
    auto* card_layout = new QVBoxLayout(file_card);
    card_layout->setContentsMargins(15, 10, 15, 10);
    card_layout->setSpacing(6);
    
    // Centered file icon (for non-image files)
    file_icon_label_ = new QLabel();
    file_icon_label_->setAlignment(Qt::AlignCenter);
    file_icon_label_->setStyleSheet("font-size: 64px;");
    card_layout->addWidget(file_icon_label_);
    
    // Preview label (for images/text) — wrapped in scroll area to prevent
    // text previews from expanding the window beyond its intended size
    preview_label_ = new QLabel();
    preview_label_->setAlignment(Qt::AlignCenter);
    preview_label_->setWordWrap(true);
    auto* preview_scroll = new QScrollArea();
    preview_scroll->setWidget(preview_label_);
    preview_scroll->setWidgetResizable(true);
    preview_scroll->setFrameShape(QFrame::NoFrame);
    preview_scroll->setStyleSheet("QScrollArea { background: transparent; }");
    card_layout->addWidget(preview_scroll, 1);
    
    // File name and info — double-click to open
    file_info_label_ = new QLabel();
    file_info_label_->setAlignment(Qt::AlignCenter);
    file_info_label_->setStyleSheet(
        "color: #ecf0f1; padding: 8px 10px; font-size: 13px; "
        "background-color: #34495e; border-radius: 4px;");
    file_info_label_->setWordWrap(true);
    file_info_label_->setCursor(Qt::PointingHandCursor);
    file_info_label_->setToolTip("Double-click to open file");
    file_info_label_->installEventFilter(this);
    card_layout->addWidget(file_info_label_);
    
    size_badge_label_ = new QLabel();
    size_badge_label_->setAlignment(Qt::AlignCenter);
    size_badge_label_->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #f39c12; "
        "padding: 4px 8px; border-radius: 4px;"
    );
    card_layout->addWidget(size_badge_label_);
    
    main_layout->addWidget(file_card, 1);  // Stretch to fill space
    
    // Progress section
    auto* progress_widget = new QWidget();
    auto* progress_vlayout = new QVBoxLayout(progress_widget);
    progress_vlayout->setContentsMargins(0, 5, 0, 5);
    progress_vlayout->setSpacing(5);
    
    progress_bar_ = new QProgressBar();
    progress_bar_->setTextVisible(true);
    progress_bar_->setFixedHeight(ui::scaling::scaled(20));
    progress_bar_->setStyleSheet(
        "QProgressBar { border: 1px solid #34495e; border-radius: 4px; text-align: center; background: #2c3e50; }"
        "QProgressBar::chunk { background-color: #3498db; }"
    );
    progress_vlayout->addWidget(progress_bar_);
    
    progress_label_ = new QLabel();
    progress_label_->setAlignment(Qt::AlignCenter);
    progress_label_->setStyleSheet("color: #bdc3c7; font-size: 11px;");
    progress_vlayout->addWidget(progress_label_);
    
    main_layout->addWidget(progress_widget);
    
    // Stats bar
    stats_label_ = new QLabel();
    stats_label_->setAlignment(Qt::AlignCenter);
    stats_label_->setStyleSheet(
        "font-size: 12px; padding: 8px; background-color: #34495e; border-radius: 4px;"
    );
    main_layout->addWidget(stats_label_);
    
    // ============ ACTION BUTTONS SECTION ============
    // New layout: Keep/Delete SIDE BY SIDE (large), Back/Skip below (thin)
    
    auto* action_widget = new QWidget();
    auto* action_layout = new QVBoxLayout(action_widget);
    action_layout->setSpacing(10);
    action_layout->setContentsMargins(20, 10, 20, 10);
    
    // Row 1: KEEP and DELETE side by side (large buttons)
    auto* main_btn_row = new QHBoxLayout();
    main_btn_row->setSpacing(20);
    
    delete_btn_ = new QPushButton("DELETE\n[Left]");
    delete_btn_->setMinimumSize(ui::scaling::scaled(ui::dimensions::kMainButtonWidth),
                                ui::scaling::scaled(ui::dimensions::kMainButtonHeight));
    delete_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    delete_btn_->setStyleSheet(QString(
        "QPushButton { font-size: 18px; font-weight: bold; "
        "background-color: %1; border: 2px solid #c0392b; color: white; border-radius: 8px; }"
        "QPushButton:hover { background-color: #c0392b; }"
    ).arg(ui::colors::kDeleteColor));
    connect(delete_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_delete);
    main_btn_row->addWidget(delete_btn_);
    
    keep_btn_ = new QPushButton("KEEP\n[Right]");
    keep_btn_->setMinimumSize(ui::scaling::scaled(ui::dimensions::kMainButtonWidth),
                              ui::scaling::scaled(ui::dimensions::kMainButtonHeight));
    keep_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    keep_btn_->setStyleSheet(QString(
        "QPushButton { font-size: 18px; font-weight: bold; "
        "background-color: %1; border: 2px solid #27ae60; color: white; border-radius: 8px; }"
        "QPushButton:hover { background-color: #27ae60; }"
    ).arg(ui::colors::kKeepColor));
    connect(keep_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_keep);
    main_btn_row->addWidget(keep_btn_);
    
    action_layout->addLayout(main_btn_row);
    
    // Row 2: Sort Later only (Back removed — use Undo instead)
    sort_later_btn_ = new QPushButton("Sort Later [↓]");
    sort_later_btn_->setFixedHeight(ui::scaling::scaled(40));
    sort_later_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sort_later_btn_->setStyleSheet(QString(
        "QPushButton { font-size: 12px; font-weight: bold; "
        "background-color: %1; border: 1px solid #d68910; color: white; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e67e22; }"
        "QPushButton:disabled { background-color: #5d4e37; color: #888; }"
    ).arg(ui::colors::kSortLaterColor));
    connect(sort_later_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_sort_later);
    action_layout->addWidget(sort_later_btn_);
    
    main_layout->addWidget(action_widget);
    
    // Bottom bar: Undo, Preview, and Finish buttons
    auto* bottom_bar = new QWidget();
    auto* bottom_layout = new QHBoxLayout(bottom_bar);
    bottom_layout->setContentsMargins(0, 5, 0, 0);
    bottom_layout->setSpacing(15);
    
    // Undo button (replaces Move to Folder in Basic Mode)
    undo_btn_ = new QPushButton("Undo [Z]");
    undo_btn_->setFixedHeight(ui::scaling::scaled(36));
    undo_btn_->setStyleSheet(
        "QPushButton { font-size: 12px; padding: 8px 15px; "
        "background-color: #9b59b6; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #8e44ad; }"
        "QPushButton:disabled { background-color: #5d4e6e; color: #888; }"
    );
    undo_btn_->setEnabled(false);  // Disabled until there's something to undo
    connect(undo_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_undo);
    bottom_layout->addWidget(undo_btn_);
    
    // Redo button
    redo_btn_ = new QPushButton("Redo [Y]");
    redo_btn_->setFixedHeight(ui::scaling::scaled(36));
    redo_btn_->setStyleSheet(
        "QPushButton { font-size: 12px; padding: 8px 15px; "
        "background-color: #9b59b6; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #8e44ad; }"
        "QPushButton:disabled { background-color: #5d4e6e; color: #888; }"
    );
    redo_btn_->setEnabled(false);  // Disabled until there's something to redo
    connect(redo_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_redo);
    bottom_layout->addWidget(redo_btn_);
    
    // Preview toggle (toggles inline preview in basic mode)
    preview_btn_ = new QPushButton("Preview [P]");
    preview_btn_->setFixedHeight(ui::scaling::scaled(36));
    preview_btn_->setCheckable(true);
    preview_btn_->setChecked(true);
    preview_btn_->setStyleSheet(QString(
        "QPushButton { font-size: 12px; padding: 8px 15px; "
        "background-color: %1; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #2980b9; }"
        "QPushButton:checked { background-color: #2980b9; border: 2px solid #1abc9c; }"
    ).arg(ui::colors::kMoveColor));
    connect(preview_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_show_preview);
    bottom_layout->addWidget(preview_btn_);
    
    // Reset Progress button
    auto* reset_btn = new QPushButton("Reset");
    reset_btn->setFixedHeight(ui::scaling::scaled(36));
    reset_btn->setStyleSheet(
        "QPushButton { font-size: 12px; padding: 8px 15px; "
        "background-color: #e74c3c; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #c0392b; }"
    );
    reset_btn->setToolTip("Reset all decisions and start over");
    connect(reset_btn, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_reset_progress);
    bottom_layout->addWidget(reset_btn);
    
    bottom_layout->addStretch();
    
    // Search box (also triggers File List window)
    search_box_ = new QLineEdit();
    search_box_->setPlaceholderText("Search files...");
    search_box_->setFixedWidth(ui::scaling::scaled(150));
    search_box_->setFixedHeight(ui::scaling::scaled(36));
    search_box_->setStyleSheet(
        "QLineEdit { padding: 4px 8px; background-color: #34495e; "
        "border-radius: 4px; color: white; border: 1px solid #4a6078; }"
        "QLineEdit:focus { border: 2px solid #3498db; }"
    );
    connect(search_box_, &QLineEdit::textChanged, this, &StandaloneFileTinderDialog::on_search);
    connect(search_box_, &QLineEdit::returnPressed, this, [this]() {
        on_search(search_box_->text());
    });
    bottom_layout->addWidget(search_box_);

    // File List button — opens separate file browser window
    auto* file_list_btn = new QPushButton("File List");
    file_list_btn->setFixedHeight(ui::scaling::scaled(36));
    file_list_btn->setStyleSheet(
        "QPushButton { font-size: 11px; padding: 6px 12px; "
        "background-color: #34495e; border-radius: 4px; color: white; border: 1px solid #4a6078; }"
        "QPushButton:hover { background-color: #3d566e; }"
    );
    file_list_btn->setToolTip("Open file list window (multi-select, drag to folders)");
    connect(file_list_btn, &QPushButton::clicked, this, [this]() {
        auto* flw = new FileListWindow(files_, filtered_indices_, current_filtered_index_, this);
        connect(flw, &FileListWindow::file_selected, this, [this](int filtered_idx) {
            if (filtered_idx >= 0 && filtered_idx < static_cast<int>(filtered_indices_.size())) {
                current_filtered_index_ = filtered_idx;
                show_current_file();
            }
        });
        connect(flw, &FileListWindow::files_assigned, this, [this](const QList<int>& indices, const QString& dest) {
            for (int fi : indices) {
                if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                    auto& file = files_[fi];
                    QString old_decision = file.decision;
                    file.decision = "move";
                    file.destination_folder = dest;
                    update_decision_count(old_decision, -1);
                    move_count_++;
                    record_action(fi, old_decision, "move", dest);
                }
            }
            update_progress();
            update_stats();
            show_current_file();
        });
        connect(flw, &FileListWindow::files_decision_changed, this, [this](const QList<int>& indices, const QString& decision) {
            for (int fi : indices) {
                if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                    auto& file = files_[fi];
                    QString old_decision = file.decision;
                    if (old_decision == decision) continue;
                    update_decision_count(old_decision, -1);
                    file.decision = decision;
                    update_decision_count(decision, 1);
                    record_action(fi, old_decision, decision, file.destination_folder);
                }
            }
            update_progress();
            update_stats();
            show_current_file();
        });
        flw->set_destination_folders(get_destination_folders());
        flw->show();
    });
    bottom_layout->addWidget(file_list_btn);
    
    bottom_layout->addSpacing(10);
    
    finish_btn_ = new QPushButton("Finish Review");
    finish_btn_->setFixedHeight(ui::scaling::scaled(36));
    finish_btn_->setStyleSheet(
        "QPushButton { font-size: 12px; padding: 8px 15px; "
        "background-color: #1abc9c; border-radius: 4px; color: white; }"
        "QPushButton:hover { background-color: #16a085; }"
    );
    connect(finish_btn_, &QPushButton::clicked, this, &StandaloneFileTinderDialog::on_finish);
    bottom_layout->addWidget(finish_btn_);
    
    main_layout->addWidget(bottom_bar);
    
    // Keyboard shortcuts hint
    shortcuts_label_ = new QLabel("Right=Keep | Left=Delete | Down=Sort Later | Z=Undo | Y=Redo | F=File List | Ctrl+F=Search | P=Preview | Enter=Finish | ?=Help");
    shortcuts_label_->setAlignment(Qt::AlignCenter);
    shortcuts_label_->setStyleSheet("color: #7f8c8d; font-size: 10px;");
    main_layout->addWidget(shortcuts_label_);
    
    update_stats();
}

void StandaloneFileTinderDialog::scan_files() {
    files_.clear();
    QMimeDatabase mime_db;
    
    QDir dir(source_folder_);
    if (!dir.exists()) {
        LOG_ERROR("BasicMode", QString("Source folder does not exist: %1").arg(source_folder_));
        QMessageBox::warning(this, "Error", "Source folder does not exist.");
        return;
    }
    
    // Include both files and directories based on settings
    QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    if (include_folders_) {
        filters |= QDir::Dirs;
    }
    
    QStringList entries = dir.entryList(filters);
    
    // Show progress for large directories (>200 files)
    const bool show_progress = entries.size() > 200;
    QProgressDialog* progress = nullptr;
    if (show_progress) {
        progress = new QProgressDialog("Scanning files...", QString(), 0, entries.size(), this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->show();
    }
    
    for (int idx = 0; idx < entries.size(); ++idx) {
        const QString& entry = entries[idx];
        try {
            QString full_path = dir.absoluteFilePath(entry);
            QFileInfo info(full_path);
            QMimeType mime_type = mime_db.mimeTypeForFile(full_path);
            
            FileToProcess file;
            file.path = full_path;
            file.name = info.fileName();
            file.extension = info.suffix().toLower();
            file.size = info.isDir() ? 0 : info.size();
            file.modified_datetime = info.lastModified();
            file.modified_date = file.modified_datetime.toString("MMM d, yyyy HH:mm");
            file.decision = "pending";
            file.mime_type = mime_type.name();
            file.is_directory = info.isDir();
            
            files_.push_back(file);
        } catch (const std::exception& ex) {
            LOG_ERROR("BasicMode", QString("Error scanning file %1: %2").arg(entry, ex.what()));
        }
        
        if (progress && (idx % 50 == 0)) {
            progress->setValue(idx);
            QApplication::processEvents();
        }
    }
    
    delete progress;
    
    LOG_INFO("BasicMode", QString("Scanned %1 files from %2").arg(files_.size()).arg(source_folder_));
    
    // Build duplicate detection cache (name+size → count)
    QHash<QPair<QString, qint64>, int> dup_map;
    for (const auto& f : files_) {
        if (!f.is_directory) {
            dup_map[{f.name, f.size}]++;
        }
    }
    for (auto& f : files_) {
        if (!f.is_directory) {
            f.has_duplicate = dup_map.value({f.name, f.size}, 0) > 1;
        }
    }
    
    // Update progress
    if (!files_.empty() && progress_bar_) {
        progress_bar_->setMaximum(static_cast<int>(files_.size()));
        progress_bar_->setValue(0);
    }
}

void StandaloneFileTinderDialog::load_session_state() {
    auto decisions = db_.get_session_decisions(source_folder_);
    
    for (const auto& decision : decisions) {
        for (auto& file : files_) {
            if (file.path == decision.file_path) {
                file.decision = decision.decision;
                file.destination_folder = decision.destination_folder;
                
                if (decision.decision == "keep") keep_count_++;
                else if (decision.decision == "delete") delete_count_++;
                else if (decision.decision == "sort_later") sort_later_count_++;
                else if (decision.decision == "move") move_count_++;
                
                break;
            }
        }
    }
    
    // Find first pending file in filtered list
    current_filtered_index_ = 0;
    for (size_t i = 0; i < filtered_indices_.size(); ++i) {
        if (files_[filtered_indices_[i]].decision == "pending") {
            current_filtered_index_ = static_cast<int>(i);
            break;
        }
    }
    
    update_progress();
    update_stats();
}

void StandaloneFileTinderDialog::save_session_state() {
    for (const auto& file : files_) {
        if (file.decision != "pending") {
            db_.save_file_decision(source_folder_, file.path, file.decision, file.destination_folder);
        }
    }
}

void StandaloneFileTinderDialog::show_current_file() {
    int file_idx = get_current_file_index();
    if (file_idx < 0 || file_idx >= static_cast<int>(files_.size())) {
        if (file_icon_label_) file_icon_label_->clear();
        if (preview_label_) preview_label_->setText("No more files to review");
        if (file_info_label_) file_info_label_->setText("");
        if (keep_btn_) keep_btn_->setEnabled(false);
        if (delete_btn_) delete_btn_->setEnabled(false);
        if (sort_later_btn_) sort_later_btn_->setEnabled(false);
        return;
    }
    
    const auto& file = files_[file_idx];
    // Only reload preview if the file actually changed (avoids zoom reset on filter/sort)
    if (file.path != current_preview_path_) {
        current_preview_path_ = file.path;
        update_preview(file.path);
    }
    update_file_info(file);
    update_progress();
    
    // Update file position header
    if (file_position_label_) {
        int pos = current_filtered_index_ + 1;
        int total = static_cast<int>(filtered_indices_.size());
        file_position_label_->setText(QString("File %1 of %2").arg(pos).arg(total));
    }
    
    // Show/hide duplicate ! button based on current file
    if (duplicate_btn_) {
        duplicate_btn_->setVisible(file.has_duplicate);
    }
    
    // Context-aware button state
    bool has_file = (file_idx >= 0);
    if (keep_btn_) keep_btn_->setEnabled(has_file);
    if (delete_btn_) delete_btn_->setEnabled(has_file);
    if (sort_later_btn_) sort_later_btn_->setEnabled(has_file);
}

void StandaloneFileTinderDialog::update_preview(const QString& file_path) {
    if (!preview_label_) return;
    
    QFileInfo finfo(file_path);
    QMimeDatabase mime_db;
    QMimeType mime_type = mime_db.mimeTypeForFile(file_path);
    QString type = mime_type.name();
    
    // Clear previous content
    if (file_icon_label_) file_icon_label_->clear();
    preview_label_->clear();
    // Reset style and alignment from any previous text file preview
    preview_label_->setStyleSheet("");
    preview_label_->setAlignment(Qt::AlignCenter);
    
    // Show file extension as the icon label
    QString ext = finfo.suffix().toUpper();
    if (ext.isEmpty()) ext = finfo.isDir() ? "DIR" : "FILE";
    QString icon = "[" + ext + "]";
    
    // Set the centered icon
    if (file_icon_label_) {
        file_icon_label_->setText(QString("<span style='font-family: monospace; font-size: 48px; "
                                          "color: #3498db; font-weight: bold;'>%1</span>").arg(icon));
    }
    
    // For images, use QImageReader with scaled size for efficient loading
    // (avoids loading full resolution into memory, then scaling — much faster for large images)
    if (type.startsWith("image/") && !finfo.isDir()) {
        QImageReader reader(file_path);
        if (reader.canRead()) {
            int max_w = preview_label_->width() > 100 ? preview_label_->width() - 20 : 400;
            int max_h = preview_label_->height() > 100 ? preview_label_->height() - 20 : 300;
            QSize orig = reader.size();
            if (orig.isValid()) {
                reader.setScaledSize(orig.scaled(max_w, max_h, Qt::KeepAspectRatio));
            }
            QImage img = reader.read();
            if (!img.isNull()) {
                preview_label_->setPixmap(QPixmap::fromImage(img));
                return;
            }
        }
    }
    
    // For text files, show content preview
    if (type.startsWith("text/") && !finfo.isDir()) {
        QFile file(file_path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            QString content = stream.read(1500);  // Reduced to avoid overflow
            if (!stream.atEnd()) {
                content += "\n...(truncated)";
            }
            preview_label_->setText(content);
            preview_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            preview_label_->setStyleSheet("color: #ecf0f1; font-family: monospace; font-size: 11px;");
            return;
        }
    }
    
    // For directories
    if (finfo.isDir()) {
        QDir dir(file_path);
        int file_count = dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count();
        int dir_count = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).count();
        preview_label_->setText(QString("Directory contains:\n%1 files\n%2 subdirectories")
                               .arg(file_count).arg(dir_count));
        return;
    }
    
    // Default: show MIME type info
    preview_label_->setText(QString("File Type: %1\n\nNo preview available").arg(mime_type.comment()));
}

void StandaloneFileTinderDialog::update_file_info(const FileToProcess& file) {
    QString size_str;
    if (file.is_directory) {
        size_str = "Directory";
    } else if (file.size < 1024) {
        size_str = QString("%1 B").arg(file.size);
    } else if (file.size < 1024 * 1024) {
        size_str = QString("%1 KB").arg(file.size / 1024.0, 0, 'f', 1);
    } else if (file.size < 1024LL * 1024 * 1024) {
        size_str = QString("%1 MB").arg(file.size / (1024.0 * 1024.0), 0, 'f', 2);
    } else {
        size_str = QString("%1 GB").arg(file.size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
    
    QString type_str = file.is_directory ? "Folder" : 
                       (file.extension.isEmpty() ? "Unknown" : file.extension.toUpper());
    
    if (!file_info_label_) return;
    
    // Duplicate detection: use cached flag from scan
    QString dup_warning;
    if (file.has_duplicate) {
        dup_warning = "<br><span style='color: #e74c3c; font-size: 11px;'>Warning: Possible duplicate found</span>";
    }
    
    file_info_label_->setText(QString("<b style='font-size: 14px;'>%1</b><br>"
                                      "<span style='color: #95a5a6;'>%2 | %3 | %4</span>%5")
                              .arg(file.name, size_str, type_str, file.modified_date, dup_warning));
    
    // Prominent file size badge
    if (size_badge_label_) {
        size_badge_label_->setText(size_str);
    }
}

void StandaloneFileTinderDialog::update_progress() {
    int filtered_total = static_cast<int>(filtered_indices_.size());
    
    // Count reviewed files within filtered set
    int filtered_reviewed = 0;
    for (int idx : filtered_indices_) {
        if (files_[idx].decision != "pending") {
            filtered_reviewed++;
        }
    }
    
    if (progress_bar_) {
        progress_bar_->setMaximum(filtered_total);
        progress_bar_->setValue(filtered_reviewed);
    }
    
    int percent = filtered_total > 0 ? (filtered_reviewed * 100 / filtered_total) : 0;
    int total = static_cast<int>(files_.size());
    QString filter_info = (current_filter_ != FileFilterType::All) 
        ? QString(" (filtered: %1 of %2)").arg(filtered_total).arg(total)
        : "";
    if (progress_label_) {
        progress_label_->setText(QString("Progress: %1 / %2 files (%3%)%4")
                                 .arg(filtered_reviewed).arg(filtered_total).arg(percent).arg(filter_info));
    }
}

void StandaloneFileTinderDialog::update_stats() {
    if (!stats_label_) return;
    
    QString stats = QString(
        "<span style='color: %1;'>Keep: %2</span>  |  "
        "<span style='color: %3;'>Delete: %4</span>  |  "
        "<span style='color: %5;'>Sort Later: %6</span>"
    ).arg(ui::colors::kKeepColor).arg(keep_count_)
     .arg(ui::colors::kDeleteColor).arg(delete_count_)
     .arg(ui::colors::kSortLaterColor).arg(sort_later_count_);
    
    // Only show Move count if there are moves (relevant in Advanced Mode)
    if (move_count_ > 0) {
        stats += QString("  |  <span style='color: %1;'>Move: %2</span>")
            .arg(ui::colors::kMoveColor).arg(move_count_);
    }
    
    stats_label_->setText(stats);
}

// Helper to update decision counts (deduplication of count logic)
void StandaloneFileTinderDialog::update_decision_count(const QString& old_decision, int delta) {
    if (old_decision == "keep") keep_count_ += delta;
    else if (old_decision == "delete") delete_count_ += delta;
    else if (old_decision == "sort_later") sort_later_count_ += delta;
    else if (old_decision == "move") move_count_ += delta;
    else if (old_decision == "copy") copy_count_ += delta;
}

int StandaloneFileTinderDialog::get_current_file_index() const {
    if (current_filtered_index_ < 0 || 
        current_filtered_index_ >= static_cast<int>(filtered_indices_.size())) {
        return -1;
    }
    return filtered_indices_[current_filtered_index_];
}

void StandaloneFileTinderDialog::record_action(int file_index, const QString& old_decision, 
                                               const QString& new_decision, const QString& old_dest_folder) {
    // Any new action clears the redo stack
    redo_stack_.clear();
    if (redo_btn_) redo_btn_->setEnabled(false);
    
    ActionRecord record;
    record.file_index = file_index;
    record.previous_decision = old_decision;
    record.new_decision = new_decision;
    record.destination_folder = old_dest_folder;  // Store old destination for undo restore
    undo_stack_.push_back(record);
    
    // Enable undo button
    if (undo_btn_) {
        undo_btn_->setEnabled(true);
    }
    
    // Save the NEW decision to DB immediately (crash safety)
    if (file_index >= 0 && file_index < static_cast<int>(files_.size())) {
        const auto& file = files_[file_index];
        db_.save_file_decision(source_folder_, file.path, file.decision, file.destination_folder);
    }
}

void StandaloneFileTinderDialog::on_keep() {
    try {
        if (animating_) return;
        int file_idx = get_current_file_index();
        if (file_idx < 0) return;
        
        auto& file = files_[file_idx];
        LOG_INFO("BasicMode", QString("Marking file as KEEP: %1").arg(file.name));
        
        QString old_decision = file.decision;
        if (old_decision != "pending") {
            update_decision_count(old_decision, -1);
        }
        
        file.decision = "keep";
        keep_count_++;
        
        // Record for undo
        record_action(file_idx, old_decision, "keep");
        
        // Visual feedback: brief flash on stats
        if (progress_label_) {
            progress_label_->setText(QString("<span style='color: %1;'>Kept: %2</span>")
                .arg(ui::colors::kKeepColor, file.name));
        }
        
        animate_swipe(true);
        advance_to_next();
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_keep: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_delete() {
    try {
        if (animating_) return;
        int file_idx = get_current_file_index();
        if (file_idx < 0) return;
        
        auto& file = files_[file_idx];
        LOG_INFO("BasicMode", QString("Marking file as DELETE: %1").arg(file.name));
        
        QString old_decision = file.decision;
        if (old_decision != "pending") {
            update_decision_count(old_decision, -1);
        }
        
        file.decision = "delete";
        delete_count_++;
        
        // Record for undo
        record_action(file_idx, old_decision, "delete");
        
        // Visual feedback
        if (progress_label_) {
            progress_label_->setText(QString("<span style='color: %1;'>Deleted: %2</span>")
                .arg(ui::colors::kDeleteColor, file.name));
        }
        
        animate_swipe(true);
        advance_to_next();
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_delete: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_sort_later() {
    try {
        if (animating_) return;
        int file_idx = get_current_file_index();
        if (file_idx < 0) return;
        
        auto& file = files_[file_idx];
        LOG_DEBUG("BasicMode", QString("Sort later file: %1").arg(file.name));
        
        QString old_decision = file.decision;
        if (old_decision != "pending") {
            update_decision_count(old_decision, -1);
        }
        
        file.decision = "sort_later";
        sort_later_count_++;
        
        // Record for undo
        record_action(file_idx, old_decision, "sort_later");
        
        // Visual feedback
        if (progress_label_) {
            progress_label_->setText(QString("<span style='color: %1;'>↓ Sort Later: %2</span>")
                .arg(ui::colors::kSortLaterColor, file.name));
        }
        
        // Move this file index to the END of filtered_indices_ so user sees it again later
        if (current_filtered_index_ >= 0 && current_filtered_index_ < static_cast<int>(filtered_indices_.size())) {
            int idx_val = filtered_indices_[current_filtered_index_];
            filtered_indices_.erase(filtered_indices_.begin() + current_filtered_index_);
            filtered_indices_.push_back(idx_val);
            // Don't increment current_filtered_index_ — the next file naturally slides into this position
        }
        
        animate_swipe(true);
        // Show the file now at current_filtered_index_ (the next file slid into position)
        if (current_filtered_index_ >= static_cast<int>(filtered_indices_.size())) {
            current_filtered_index_ = 0;  // Wrap around if at end
        }
        update_stats();
        update_progress();
        show_current_file();
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_sort_later: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_undo() {
    try {
        if (undo_stack_.empty()) {
            LOG_DEBUG("BasicMode", "Nothing to undo");
            return;
        }
        
        // Pop the last action
        ActionRecord last_action = undo_stack_.back();
        undo_stack_.pop_back();
        
        // Push onto redo stack
        redo_stack_.push_back(last_action);
        if (redo_btn_) redo_btn_->setEnabled(true);
        
        // Revert the file's decision
        auto& file = files_[last_action.file_index];
        LOG_INFO("BasicMode", QString("Undoing action on file: %1 (was %2, reverting to %3)")
                             .arg(file.name, last_action.new_decision, last_action.previous_decision));
        
        // Decrement count for the action we're undoing
        update_decision_count(last_action.new_decision, -1);
        
        // Restore previous decision
        file.decision = last_action.previous_decision;
        file.destination_folder = last_action.destination_folder;
        
        // Save restored decision to DB immediately
        db_.save_file_decision(source_folder_, file.path, 
                              last_action.previous_decision, last_action.destination_folder);
        
        // Increment count for the restored decision (if not pending)
        if (last_action.previous_decision != "pending") {
            update_decision_count(last_action.previous_decision, 1);
        }
        
        // Navigate to the undone file
        for (int i = 0; i < static_cast<int>(filtered_indices_.size()); ++i) {
            if (filtered_indices_[i] == last_action.file_index) {
                current_filtered_index_ = i;
                break;
            }
        }
        
        // Update UI
        update_stats();
        update_progress();
        show_current_file();
        
        // Disable undo button if stack is empty
        if (undo_stack_.empty() && undo_btn_) {
            undo_btn_->setEnabled(false);
        }
        
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_undo: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_redo() {
    try {
        if (redo_stack_.empty()) {
            LOG_DEBUG("BasicMode", "Nothing to redo");
            return;
        }
        
        // Pop the last undone action
        ActionRecord action = redo_stack_.back();
        redo_stack_.pop_back();
        
        auto& file = files_[action.file_index];
        LOG_INFO("BasicMode", QString("Redoing action on file: %1 (applying %2)")
                             .arg(file.name, action.new_decision));
        
        // Revert the restored decision count
        if (file.decision != "pending") {
            update_decision_count(file.decision, -1);
        }
        
        // Reapply the new decision
        file.decision = action.new_decision;
        if (action.new_decision == "move" || action.new_decision == "copy") {
            file.destination_folder = action.destination_folder;
        }
        update_decision_count(action.new_decision, 1);
        
        // Save to DB
        db_.save_file_decision(source_folder_, file.path, file.decision, file.destination_folder);
        
        // Push back onto undo stack
        undo_stack_.push_back(action);
        if (undo_btn_) undo_btn_->setEnabled(true);
        
        // Navigate to the file
        for (int i = 0; i < static_cast<int>(filtered_indices_.size()); ++i) {
            if (filtered_indices_[i] == action.file_index) {
                current_filtered_index_ = i;
                break;
            }
        }
        
        // Update UI
        update_stats();
        update_progress();
        show_current_file();
        
        if (redo_stack_.empty() && redo_btn_) {
            redo_btn_->setEnabled(false);
        }
        
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_redo: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_show_preview() {
    try {
        // Toggle inline preview visibility in basic mode
        if (preview_label_) {
            bool showing = preview_label_->isVisible();
            preview_label_->setVisible(!showing);
            if (file_icon_label_) file_icon_label_->setVisible(!showing);
            if (preview_btn_) preview_btn_->setChecked(!showing);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_show_preview: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::on_reset_progress() {
    int reviewed = keep_count_ + delete_count_ + sort_later_count_ + move_count_;
    if (reviewed == 0) {
        QMessageBox::information(this, "Nothing to Reset", "No decisions have been made yet.");
        return;
    }
    
    auto reply = QMessageBox::question(this, "Reset Progress",
        QString("This will clear all %1 decisions and start over.\n\n"
                "This cannot be undone. Continue?").arg(reviewed),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (reply != QMessageBox::Yes) return;
    
    // Reset all decisions
    for (auto& file : files_) {
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
    
    // Clear from database
    db_.clear_session(source_folder_);
    
    // Reset to first file
    current_filtered_index_ = 0;
    
    update_stats();
    update_progress();
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    
    LOG_INFO("BasicMode", "Progress reset by user");
}

void StandaloneFileTinderDialog::on_finish() {
    try {
        LOG_INFO("BasicMode", "Finishing review, showing summary");
        show_review_summary();
    } catch (const std::exception& ex) {
        LOG_ERROR("BasicMode", QString("Error in on_finish: %1").arg(ex.what()));
        QMessageBox::warning(this, "Error", QString("An error occurred: %1").arg(ex.what()));
    }
}

void StandaloneFileTinderDialog::advance_to_next() {
    update_stats();
    update_progress();
    
    // Find next pending file in filtered list
    int start = current_filtered_index_ + 1;
    for (int i = start; i < static_cast<int>(filtered_indices_.size()); ++i) {
        if (files_[filtered_indices_[i]].decision == "pending") {
            current_filtered_index_ = i;
            show_current_file();
            return;
        }
    }
    
    // No more pending files in filter
    current_filtered_index_ = static_cast<int>(filtered_indices_.size());
    if (file_icon_label_) file_icon_label_->clear();
    if (preview_label_) {
        preview_label_->setText("<div style='text-align: center; font-size: 18px; color: #2ecc71; margin-top: 40px;'>"
                                "All files reviewed!</div>");
    }
    if (file_info_label_) {
        file_info_label_->setText("Click 'Finish Review' to execute your decisions.");
    }
    if (finish_btn_) {
        finish_btn_->setStyleSheet(
            "QPushButton { font-size: 12px; padding: 8px 15px; "
            "background-color: #2ecc71; border-radius: 4px; color: white; "
            "border: 2px solid #f1c40f; }"
            "QPushButton:hover { background-color: #27ae60; }"
        );
    }
}

void StandaloneFileTinderDialog::go_to_previous() {
    if (current_filtered_index_ <= 0) return;
    
    // Find previous file in filtered list
    for (int i = current_filtered_index_ - 1; i >= 0; --i) {
        current_filtered_index_ = i;
        show_current_file();
        return;
    }
}

void StandaloneFileTinderDialog::show_review_summary() {
    QDialog summary_dialog(this);
    summary_dialog.setWindowTitle("Review Summary");
    summary_dialog.setMinimumSize(ui::scaling::scaled(800), ui::scaling::scaled(550));
    
    auto* layout = new QVBoxLayout(&summary_dialog);
    
    // Summary stats
    auto* stats_widget = new QWidget();
    stats_widget->setStyleSheet("background-color: #34495e; border-radius: 8px; padding: 15px;");
    auto* stats_layout = new QHBoxLayout(stats_widget);
    
    auto create_stat_box = [](const QString& label, int count, const QString& color) {
        auto* box = new QWidget();
        box->setStyleSheet(QString("background-color: %1; border-radius: 6px; padding: 10px;").arg(color));
        auto* box_layout = new QVBoxLayout(box);
        auto* count_label = new QLabel(QString::number(count));
        count_label->setStyleSheet("font-size: 24px; font-weight: bold; color: white;");
        count_label->setAlignment(Qt::AlignCenter);
        auto* text_label = new QLabel(label);
        text_label->setStyleSheet("color: white;");
        text_label->setAlignment(Qt::AlignCenter);
        box_layout->addWidget(count_label);
        box_layout->addWidget(text_label);
        return box;
    };
    
    stats_layout->addWidget(create_stat_box("Keep", keep_count_, ui::colors::kKeepColor));
    stats_layout->addWidget(create_stat_box("Delete", delete_count_, ui::colors::kDeleteColor));
    stats_layout->addWidget(create_stat_box("Sort Later", sort_later_count_, ui::colors::kSortLaterColor));
    stats_layout->addWidget(create_stat_box("Move", move_count_, ui::colors::kMoveColor));
    if (copy_count_ > 0)
        stats_layout->addWidget(create_stat_box("Copy", copy_count_, "#8e44ad"));
    
    layout->addWidget(stats_widget);
    
    // Requirement 6 + 22-23: Editable per-file review table with mode column
    auto* files_label = new QLabel("File Decisions (click Decision to change):");
    files_label->setStyleSheet("font-weight: bold; margin-top: 10px;");
    layout->addWidget(files_label);
    
    // Bulk action toolbar
    auto* bulk_bar = new QHBoxLayout();
    bulk_bar->addWidget(new QLabel("Bulk:"));
    auto* bulk_keep_btn = new QPushButton("All Keep");
    bulk_keep_btn->setMaximumWidth(70);
    bulk_keep_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    bulk_bar->addWidget(bulk_keep_btn);
    auto* bulk_sort_later_btn = new QPushButton("All Sort Later");
    bulk_sort_later_btn->setMaximumWidth(90);
    bulk_sort_later_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    bulk_bar->addWidget(bulk_sort_later_btn);
    auto* bulk_pending_btn = new QPushButton("All Pending");
    bulk_pending_btn->setMaximumWidth(80);
    bulk_pending_btn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 6px; }");
    bulk_bar->addWidget(bulk_pending_btn);
    bulk_bar->addStretch();
    layout->addLayout(bulk_bar);
    
    auto* table = new QTableWidget();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"File", "Size", "Decision", "Destination", "Mode"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Filter/sort bar for review table
    auto* review_filter_layout = new QHBoxLayout();
    review_filter_layout->addWidget(new QLabel("Filter:"));
    auto* review_filter_combo = new QComboBox();
    review_filter_combo->addItems({"All", "Keep", "Delete", "Sort Later", "Move", "Copy", "Pending"});
    review_filter_layout->addWidget(review_filter_combo);
    review_filter_layout->addSpacing(16);
    review_filter_layout->addWidget(new QLabel("Sort:"));
    auto* review_sort_combo = new QComboBox();
    review_sort_combo->addItems({"Original Order", "By Name", "By Decision", "By Destination"});
    review_filter_layout->addWidget(review_sort_combo);
    review_filter_layout->addStretch();
    auto* preview_toggle = new QCheckBox("Preview on hover");
    preview_toggle->setChecked(false);
    preview_toggle->setToolTip("Show file details when hovering over rows");
    review_filter_layout->addWidget(preview_toggle);
    layout->addLayout(review_filter_layout);
    
    // Determine mode name for this dialog
    QString mode_name = "Basic";
    if (windowTitle().contains("AI Mode")) mode_name = "AI";
    else if (windowTitle().contains("Advanced")) mode_name = "Advanced";
    
    // Performance note for large file sets
    if (files_.size() > 500) {
        auto* perf_note = new QLabel(QString("Loading %1 files...").arg(files_.size()));
        perf_note->setStyleSheet("color: #f39c12; font-size: 10px;");
        layout->addWidget(perf_note);
        QApplication::processEvents();
    }

    // Populate ALL files (including pending)
    int visible_row = 0;
    std::vector<int> row_to_file_idx;
    table->setRowCount(static_cast<int>(files_.size()));
    
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
        const auto& file = files_[i];
        
        row_to_file_idx.push_back(i);
        
        // File name (read-only)
        auto* name_item = new QTableWidgetItem(file.name);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        name_item->setData(Qt::UserRole, i);

        // Visual color based on decision
        QColor row_color;
        if (file.decision == "keep") row_color = QColor("#27ae60");
        else if (file.decision == "delete") row_color = QColor("#e74c3c");
        else if (file.decision == "move") row_color = QColor("#3498db");
        else if (file.decision == "copy") row_color = QColor("#9b59b6");
        else if (file.decision == "sort_later") row_color = QColor("#f39c12");
        else if (file.decision == "pending") row_color = QColor("#888");
        else row_color = QColor("#888");
        name_item->setForeground(row_color);

        table->setItem(visible_row, 0, name_item);

        // Size column
        auto* size_item = new QTableWidgetItem(QLocale().formattedDataSize(file.size, 1, QLocale::DataSizeTraditionalFormat));
        size_item->setFlags(size_item->flags() & ~Qt::ItemIsEditable);
        size_item->setData(Qt::UserRole, file.size);  // for sorting
        table->setItem(visible_row, 1, size_item);
        
        // Decision (editable via combo box)
        auto* combo = new QComboBox();
        combo->addItems({"keep", "delete", "sort_later", "move", "copy", "pending"});
        combo->setCurrentText(file.decision);
        table->setCellWidget(visible_row, 2, combo);
        
        // Destination (editable dropdown — type paths or select from grid/AI suggestions)
        auto* dest_combo = new QComboBox();
        dest_combo->setEditable(true);
        dest_combo->addItem("(none)", QString());
        // Add grid folders with disambiguated labels
        QStringList grid_folders = get_destination_folders();
        // Count basenames to detect ambiguity
        QMap<QString, int> basename_counts;
        for (const QString& fp : grid_folders)
            basename_counts[QFileInfo(fp).fileName()]++;
        for (const QString& fp : grid_folders) {
            QString basename = QFileInfo(fp).fileName();
            QString label = basename;
            if (basename_counts.value(basename, 0) > 1) {
                // Disambiguate: show parent/name
                label = QFileInfo(fp).dir().dirName() + "/" + basename;
            }
            if (!QDir(fp).exists()) label += " [virtual]";
            dest_combo->addItem(label, fp);
        }
        // Set current destination
        if (!file.destination_folder.isEmpty()) {
            int idx_found = dest_combo->findData(file.destination_folder);
            if (idx_found >= 0) {
                dest_combo->setCurrentIndex(idx_found);
            } else {
                // Not in grid — add it
                QString label = QFileInfo(file.destination_folder).fileName();
                if (!QDir(file.destination_folder).exists()) label += " [virtual]";
                dest_combo->addItem(label, file.destination_folder);
                dest_combo->setCurrentIndex(dest_combo->count() - 1);
            }
        }
        dest_combo->setToolTip("Type a folder path or select from dropdown. Relative paths are under the source folder.");
        table->setCellWidget(visible_row, 3, dest_combo);
        
        // Mode column: moves with destinations from Advanced/AI modes; others from current mode
        QString mode_for_row = mode_name;
        if (file.decision == "move" && !file.destination_folder.isEmpty()
            && mode_name == "Basic") {
            mode_for_row = "Advanced";
        }
        auto* mode_item = new QTableWidgetItem(mode_for_row);
        mode_item->setFlags(mode_item->flags() & ~Qt::ItemIsEditable);
        table->setItem(visible_row, 4, mode_item);
        
        visible_row++;
    }
    
    table->resizeColumnsToContents();
    table->setColumnWidth(0, qMax(table->columnWidth(0), 200));
    layout->addWidget(table, 1);

    // Connect preview-on-hover toggle
    connect(preview_toggle, &QCheckBox::toggled, this, [this, table](bool checked) {
        for (int row = 0; row < table->rowCount(); ++row) {
            auto* item = table->item(row, 0);
            if (item && checked) {
                int idx = item->data(Qt::UserRole).toInt();
                if (idx >= 0 && idx < static_cast<int>(files_.size())) {
                    const auto& f = files_[idx];
                    QString tip = QString("Path: %1\nSize: %2\nType: %3")
                        .arg(f.path)
                        .arg(QLocale().formattedDataSize(f.size))
                        .arg(f.mime_type);
                    item->setToolTip(tip);
                }
            } else if (item) {
                item->setToolTip("");
            }
        }
    });

    // Connect bulk action buttons
    connect(bulk_keep_btn, &QPushButton::clicked, this, [table]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* combo = qobject_cast<QComboBox*>(table->cellWidget(r, 2));
            if (combo) combo->setCurrentText("keep");
        }
    });
    connect(bulk_sort_later_btn, &QPushButton::clicked, this, [table]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* combo = qobject_cast<QComboBox*>(table->cellWidget(r, 2));
            if (combo) combo->setCurrentText("sort_later");
        }
    });
    connect(bulk_pending_btn, &QPushButton::clicked, this, [table]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* combo = qobject_cast<QComboBox*>(table->cellWidget(r, 2));
            if (combo) combo->setCurrentText("pending");
        }
    });
    
    connect(review_filter_combo, &QComboBox::currentTextChanged, this, [table](const QString& filter) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (filter == "All") {
                table->setRowHidden(row, false);
                continue;
            }
            auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, 2));
            if (combo) {
                // Map display filter names to internal decision values
                QString filter_value = filter.toLower().replace(" ", "_");
                bool match = (combo->currentText() == filter_value);
                table->setRowHidden(row, !match);
            }
        }
    });
    
    // Folder creation note
    int new_folder_count = 0;
    QSet<QString> dest_folders;
    for (const auto& file : files_) {
        if (file.decision == "move" && !file.destination_folder.isEmpty()) {
            dest_folders.insert(file.destination_folder);
        }
    }
    for (const QString& folder : dest_folders) {
        if (!QDir(folder).exists()) new_folder_count++;
    }
    if (new_folder_count > 0) {
        auto* note = new QLabel(
            QString("Note: %1 folder(s) will be created during execution.").arg(new_folder_count));
        note->setStyleSheet("color: #f39c12; font-style: italic;");
        layout->addWidget(note);
    }
    
    // Buttons
    auto* btn_layout = new QHBoxLayout();
    
    auto* cancel_btn = new QPushButton("Cancel");
    cancel_btn->setStyleSheet("QPushButton { padding: 8px 16px; background-color: #7f8c8d; color: white; border-radius: 4px; }"
                              "QPushButton:hover { background-color: #95a5a6; }");
    connect(cancel_btn, &QPushButton::clicked, &summary_dialog, &QDialog::reject);
    btn_layout->addWidget(cancel_btn);
    
    btn_layout->addStretch();
    
    auto* execute_btn = new QPushButton("Execute All");
    execute_btn->setStyleSheet(
        "QPushButton { background-color: #2ecc71; color: white; font-weight: bold; "
        "padding: 10px 20px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #27ae60; }"
    );
    connect(execute_btn, &QPushButton::clicked, &summary_dialog, [&, table]() {
        // Apply any edits from combo boxes back to files_
        for (int r = 0; r < table->rowCount() && r < static_cast<int>(row_to_file_idx.size()); ++r) {
            auto* combo = qobject_cast<QComboBox*>(table->cellWidget(r, 2));
            if (!combo) continue;
            
            int file_idx = row_to_file_idx[r];
            auto& file = files_[file_idx];
            QString new_decision = combo->currentText();
            
            // Read destination from dest combo
            auto* dest_combo = qobject_cast<QComboBox*>(table->cellWidget(r, 3));
            QString new_dest;
            if (dest_combo) {
                new_dest = dest_combo->currentData().toString();
                // If user typed a custom path (editable combo), use text
                if (new_dest.isEmpty() && dest_combo->currentText() != "(none)") {
                    QString typed = dest_combo->currentText().trimmed();
                    if (!typed.isEmpty()) {
                        if (!typed.startsWith("/") && !typed.contains(":/")) {
                            typed = QDir::cleanPath(source_folder_ + "/" + typed);
                        }
                        new_dest = typed;
                    }
                }
            }
            
            if (new_decision != file.decision || new_dest != file.destination_folder) {
                update_decision_count(file.decision, -1);
                if (new_decision != "pending") {
                    update_decision_count(new_decision, 1);
                }
                // If decision is "move" or "copy" and has a destination, set it
                if ((new_decision == "move" || new_decision == "copy") && !new_dest.isEmpty()) {
                    file.destination_folder = new_dest;
                } else if (new_decision != "move" && new_decision != "copy") {
                    file.destination_folder.clear();
                }
                file.decision = new_decision;
            }
        }
        
        summary_dialog.accept();
        execute_decisions();
    });
    btn_layout->addWidget(execute_btn);
    
    layout->addLayout(btn_layout);
    
    auto* shortcut_hint = new QLabel("Shortcuts: Ctrl+A = Select all rows");
    shortcut_hint->setStyleSheet("color: #666; font-size: 10px;");
    layout->addWidget(shortcut_hint);
    
    summary_dialog.exec();
}

void StandaloneFileTinderDialog::execute_decisions() {
    ExecutionPlan plan;
    
    // Collect unique destination folders from move decisions
    QSet<QString> dest_folders;
    
    std::vector<std::pair<QString, QString>> files_to_copy;
    for (const auto& file : files_) {
        if (file.decision == "delete") {
            plan.files_to_delete.push_back(file.path);
        } else if (file.decision == "move" && !file.destination_folder.isEmpty()) {
            plan.files_to_move.push_back({file.path, file.destination_folder});
            dest_folders.insert(file.destination_folder);
        } else if (file.decision == "copy" && !file.destination_folder.isEmpty()) {
            files_to_copy.push_back({file.path, file.destination_folder});
            dest_folders.insert(file.destination_folder);
        }
    }
    
    // Ensure all destination folders exist (handles virtual folders from advanced mode)
    for (const QString& folder : dest_folders) {
        if (!QDir(folder).exists()) {
            plan.folders_to_create.push_back(folder);
        }
    }
    
    // Requirement 20: Verify we can create virtual folders before proceeding
    QSet<QString> failed_folders;
    for (const QString& folder : plan.folders_to_create) {
        if (!QDir().mkpath(folder)) {
            failed_folders.insert(folder);
            auto reply = QMessageBox::warning(this, "Folder Creation Failed",
                QString("Could not create folder:\n%1\n\n"
                        "Files assigned to this folder will be skipped.\n"
                        "Continue with remaining operations?").arg(folder),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (reply != QMessageBox::Yes) return;
        }
    }
    // Clear the create list since we handled it above
    plan.folders_to_create.clear();
    
    // Execute copy operations
    for (const auto& [src, dest_folder] : files_to_copy) {
        if (failed_folders.contains(dest_folder)) continue;
        QString dest_path = QDir::cleanPath(dest_folder + "/" + QFileInfo(src).fileName());
        if (!QFile::copy(src, dest_path)) {
            LOG_WARN("Execute", QString("Failed to copy: %1 -> %2 (error: %3)")
                .arg(src, dest_path, QFile(src).errorString()));
        }
    }
    
    // Remove moves targeting failed folders
    if (!failed_folders.isEmpty()) {
        plan.files_to_move.erase(
            std::remove_if(plan.files_to_move.begin(), plan.files_to_move.end(),
                [&](const auto& pair) { return failed_folders.contains(pair.second); }),
            plan.files_to_move.end());
    }
    
    // Progress dialog
    QProgressDialog progress("Executing decisions...", "Cancel", 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();
    
    QElapsedTimer timer;
    timer.start();
    
    FileTinderExecutor executor;
    auto result = executor.execute(plan, [&](int current, int total, const QString& msg) {
        if (total > 0) {
            progress.setValue(current * 100 / total);
        }
        progress.setLabelText(msg);
        QApplication::processEvents();
    });
    
    qint64 elapsed_ms = timer.elapsed();
    progress.close();
    
    // Save execution log to database for undo support
    for (const auto& entry : result.log) {
        if (entry.success) {
            db_.save_execution_log(source_folder_, entry.action, entry.source_path, entry.dest_path);
        }
    }
    
    // Show execution results dialog with undo + stats
    show_execution_results(result, elapsed_ms);
    
    // Clear session decisions (but execution log persists for undo)
    db_.clear_session(source_folder_);
    
    emit session_completed();
    accept();
}

void StandaloneFileTinderDialog::show_execution_results(const ExecutionResult& result, qint64 elapsed_ms) {
    QDialog results_dialog(this);
    results_dialog.setWindowTitle("Execution Complete");
    results_dialog.setMinimumSize(ui::scaling::scaled(700), ui::scaling::scaled(500));
    
    auto* layout = new QVBoxLayout(&results_dialog);
    
    // Session statistics (Req 9)
    auto* stats_group = new QGroupBox("Session Statistics");
    stats_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* stats_layout = new QVBoxLayout(stats_group);
    
    int total_files = static_cast<int>(files_.size());
    int total_reviewed = keep_count_ + delete_count_ + sort_later_count_ + move_count_;
    double elapsed_sec = elapsed_ms / 1000.0;
    // Review pace uses session time (from dialog open to finish), not execution time
    double session_sec = session_timer_.elapsed() / 1000.0;
    double files_per_min = session_sec > 0 ? (total_reviewed * 60.0 / session_sec) : 0;
    
    auto* stats_text = new QLabel(QString(
        "Total files scanned: %1\n"
        "Files reviewed: %2\n"
        "  • Kept: %3\n"
        "  • Deleted: %4\n"
        "  • Sort Later: %5\n"
        "  • Moved: %6\n\n"
        "Execution time: %7s\n"
        "Files moved: %8 | Files deleted: %9 | Errors: %10"
    ).arg(total_files).arg(total_reviewed)
     .arg(keep_count_).arg(delete_count_).arg(sort_later_count_).arg(move_count_)
     .arg(elapsed_sec, 0, 'f', 1)
     .arg(result.files_moved).arg(result.files_deleted).arg(result.errors));
    stats_text->setStyleSheet("font-size: 12px; padding: 8px;");
    stats_layout->addWidget(stats_text);
    
    if (files_per_min > 0) {
        auto* pace_label = new QLabel(QString("Review pace: ~%1 files/minute")
            .arg(files_per_min, 0, 'f', 1));
        pace_label->setStyleSheet("color: #3498db; font-style: italic;");
        stats_layout->addWidget(pace_label);
    }
    
    // Calculate disk space freed by deletions
    qint64 space_freed = 0;
    for (const auto& f : files_) {
        if (f.decision == "delete") {
            space_freed += f.size;
        }
    }
    if (space_freed > 0) {
        QString size_str;
        if (space_freed < 1024LL) size_str = QString("%1 B").arg(space_freed);
        else if (space_freed < 1024LL*1024) size_str = QString("%1 KB").arg(space_freed/1024.0, 0, 'f', 1);
        else if (space_freed < 1024LL*1024*1024) size_str = QString("%1 MB").arg(space_freed/(1024.0*1024.0), 0, 'f', 2);
        else size_str = QString("%1 GB").arg(space_freed/(1024.0*1024.0*1024.0), 0, 'f', 2);
        auto* space_label = new QLabel(QString("<span style='color: #e74c3c;'>Disk space freed: %1</span>").arg(size_str));
        stats_layout->addWidget(space_label);
    }
    
    layout->addWidget(stats_group);
    
    if (!result.error_messages.isEmpty()) {
        auto* errors_label = new QLabel("Errors:\n" + result.error_messages.join("\n"));
        errors_label->setStyleSheet("color: #e74c3c; font-size: 11px; padding: 4px;");
        errors_label->setWordWrap(true);
        layout->addWidget(errors_label);
    }
    
    // Execution log with undo (Req 8)
    auto successful_entries = result.log;
    successful_entries.erase(
        std::remove_if(successful_entries.begin(), successful_entries.end(),
            [](const ExecutionLogEntry& e) { return !e.success; }),
        successful_entries.end());
    
    if (!successful_entries.empty()) {
        auto* log_label = new QLabel(QString("Execution Log (%1 actions — click Undo to reverse):")
            .arg(successful_entries.size()));
        log_label->setStyleSheet("font-weight: bold; margin-top: 8px;");
        layout->addWidget(log_label);
        
        auto* table = new QTableWidget();
        table->setColumnCount(4);
        table->setHorizontalHeaderLabels({"Action", "File", "Destination", "Undo"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setRowCount(static_cast<int>(successful_entries.size()));
        
        auto* log_filter_layout = new QHBoxLayout();
        log_filter_layout->addWidget(new QLabel("Filter:"));
        auto* log_filter_combo = new QComboBox();
        log_filter_combo->addItems({"All", "Moved", "Deleted", "Kept"});
        log_filter_layout->addWidget(log_filter_combo);
        log_filter_layout->addStretch();
        
        auto* batch_undo_btn = new QPushButton("Undo All");
        batch_undo_btn->setStyleSheet("QPushButton { padding: 4px 12px; background-color: #e67e22; color: white; border-radius: 3px; }"
                                      "QPushButton:hover { background-color: #d35400; }");
        batch_undo_btn->setToolTip("Undo all reversible operations");
        log_filter_layout->addWidget(batch_undo_btn);

        auto* export_btn = new QPushButton("Export Log");
        export_btn->setStyleSheet("QPushButton { padding: 4px 12px; background-color: #3498db; color: white; border-radius: 3px; }"
                                  "QPushButton:hover { background-color: #2980b9; }");
        log_filter_layout->addWidget(export_btn);
        
        layout->addLayout(log_filter_layout);
        
        for (int i = 0; i < static_cast<int>(successful_entries.size()); ++i) {
            const auto& entry = successful_entries[i];
            
            auto* action_item = new QTableWidgetItem(entry.action);
            action_item->setFlags(action_item->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, action_item);
            
            auto* file_item = new QTableWidgetItem(QFileInfo(entry.source_path).fileName());
            file_item->setFlags(file_item->flags() & ~Qt::ItemIsEditable);
            file_item->setToolTip(entry.source_path);
            table->setItem(i, 1, file_item);
            
            QString dest_display;
            if (!entry.dest_path.isEmpty()) {
                if (entry.action == "delete") {
                    dest_display = "(trash)";
                } else if (entry.dest_path.startsWith(source_folder_)) {
                    // Show relative to source folder
                    dest_display = entry.dest_path.mid(source_folder_.length());
                    if (dest_display.startsWith('/')) dest_display = dest_display.mid(1);
                    if (dest_display.isEmpty()) dest_display = ".";
                } else {
                    dest_display = entry.dest_path;
                }
            } else if (entry.action == "delete") {
                dest_display = "(permanent)";
            }
            auto* dest_item = new QTableWidgetItem(dest_display);
            dest_item->setFlags(dest_item->flags() & ~Qt::ItemIsEditable);
            dest_item->setToolTip(entry.dest_path);
            table->setItem(i, 2, dest_item);
            
            auto* undo_btn = new QPushButton("Undo");
            undo_btn->setFixedWidth(60);
            undo_btn->setStyleSheet(
                "QPushButton { background-color: #e67e22; color: white; padding: 2px 6px; border-radius: 3px; }"
                "QPushButton:hover { background-color: #d35400; }"
                "QPushButton:disabled { background-color: #7f8c8d; color: #bdc3c7; }"
            );
            if (entry.action == "delete" && entry.dest_path.isEmpty()) {
                undo_btn->setToolTip("File was permanently deleted — undo will attempt recovery");
            }
            connect(undo_btn, &QPushButton::clicked, this, [this, entry, undo_btn, action_item]() {
                if (FileTinderExecutor::undo_action(entry)) {
                    undo_btn->setEnabled(false);
                    undo_btn->setText("Done");
                    action_item->setText(entry.action + " (undone)");
                    
                    // Remove from database log
                    auto log_entries = db_.get_execution_log(source_folder_);
                    for (const auto& [id, action, src, dst, ts] : log_entries) {
                        if (src == entry.source_path && action == entry.action) {
                            db_.remove_execution_log_entry(id);
                            break;
                        }
                    }
                    
                    LOG_INFO("Execution", QString("Undone: %1 %2").arg(entry.action, entry.source_path));
                } else {
                    QMessageBox::warning(undo_btn->parentWidget(), "Undo Failed",
                        QString("Could not undo this action.\nThe file may have been modified or removed."));
                }
            });
            table->setCellWidget(i, 3, undo_btn);
        }
        
        table->resizeColumnsToContents();
        table->setColumnWidth(1, qMax(table->columnWidth(1), 200));
        layout->addWidget(table, 1);
        
        connect(log_filter_combo, &QComboBox::currentTextChanged, this, [table](const QString& filter) {
            for (int row = 0; row < table->rowCount(); ++row) {
                if (filter == "All") {
                    table->setRowHidden(row, false);
                    continue;
                }
                auto* item = table->item(row, 0);
                if (item) {
                    QString action = item->text().toLower();
                    // Map filter labels to action strings: "Moved"→"move", "Deleted"→"delete", "Kept"→"keep"
                    QString expected;
                    if (filter == "Moved") expected = "move";
                    else if (filter == "Deleted") expected = "delete";
                    else if (filter == "Kept") expected = "keep";
                    else expected = filter.toLower();
                    bool match = action.startsWith(expected);
                    table->setRowHidden(row, !match);
                }
            }
        });
        
        connect(batch_undo_btn, &QPushButton::clicked, this, [table]() {
            auto reply = QMessageBox::question(nullptr, "Undo All",
                "This will undo all reversible operations. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
            for (int row = 0; row < table->rowCount(); ++row) {
                auto* btn = qobject_cast<QPushButton*>(table->cellWidget(row, table->columnCount() - 1));
                if (btn && btn->isEnabled() && btn->text() == "Undo") {
                    btn->click();
                }
            }
        });
        
        connect(export_btn, &QPushButton::clicked, this, [table]() {
            QString filename = QFileDialog::getSaveFileName(nullptr, "Export Execution Log",
                QDir::homePath() + "/file_tinder_log.csv", "CSV Files (*.csv)");
            if (filename.isEmpty()) return;
            QFile file(filename);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << "Action,File,Destination,Status\n";
                for (int row = 0; row < table->rowCount(); ++row) {
                    QStringList cols;
                    for (int col = 0; col < table->columnCount() - 1; ++col) {
                        auto* item = table->item(row, col);
                        cols << (item ? "\"" + item->text().replace("\"", "\"\"") + "\"" : "\"\"");
                    }
                    out << cols.join(",") << "\n";
                }
                file.close();
                QMessageBox::information(nullptr, "Exported", QString("Log exported to %1").arg(filename));
            }
        });
    }
    
    // Close button
    auto* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    
    auto* close_btn = new QPushButton("Consolidate");
    close_btn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "padding: 10px 25px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    close_btn->setToolTip("Finalize all changes — undone operations will be permanent after this");
    connect(close_btn, &QPushButton::clicked, &results_dialog, &QDialog::accept);
    btn_layout->addWidget(close_btn);
    
    layout->addLayout(btn_layout);
    
    results_dialog.exec();
}

void StandaloneFileTinderDialog::keyPressEvent(QKeyEvent* event) {
    // Don't intercept keys when a text input has focus
    if (search_box_ && search_box_->hasFocus()) {
        QDialog::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
        case Qt::Key_Right:
            on_keep();
            break;
        case Qt::Key_Left:
            on_delete();
            break;
        case Qt::Key_Down:
            on_sort_later();
            break;
        case Qt::Key_Up:
            // Up arrow is no longer Back — use Z for Undo instead
            break;
        case Qt::Key_Backspace:
            on_undo();
            break;
        case Qt::Key_Z:
            on_undo();
            break;
        case Qt::Key_Y:
            on_redo();
            break;
        case Qt::Key_P:
            on_show_preview();
            break;
        case Qt::Key_F:
            // F opens the File List window
            if (event->modifiers() & Qt::ControlModifier) {
                // Ctrl+F focuses search box
                if (search_box_) { search_box_->setFocus(); search_box_->selectAll(); }
            } else {
                // F opens File List window (same as clicking "File List" button)
                {
                    auto* flw = new FileListWindow(files_, filtered_indices_, current_filtered_index_, this);
                    connect(flw, &FileListWindow::file_selected, this, [this](int filtered_idx) {
                        if (filtered_idx >= 0 && filtered_idx < static_cast<int>(filtered_indices_.size())) {
                            current_filtered_index_ = filtered_idx;
                            show_current_file();
                        }
                    });
                    connect(flw, &FileListWindow::files_assigned, this, [this](const QList<int>& indices, const QString& dest) {
                        for (int fi : indices) {
                            if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                                auto& file = files_[fi];
                                QString old_decision = file.decision;
                                file.decision = "move";
                                file.destination_folder = dest;
                                update_decision_count(old_decision, -1);
                                move_count_++;
                                record_action(fi, old_decision, "move", dest);
                            }
                        }
                        update_progress();
                        update_stats();
                        show_current_file();
                    });
                    flw->set_destination_folders(get_destination_folders());
                    flw->show();
                }
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            on_finish();
            break;
        case Qt::Key_Question:
        case Qt::Key_Slash:
            if (event->modifiers() & Qt::ShiftModifier) {
                show_shortcuts_help();
            }
            break;
        default:
            QDialog::keyPressEvent(event);
    }
}

void StandaloneFileTinderDialog::closeEvent(QCloseEvent* event) {
    // Don't delegate to QDialog::closeEvent — it accepts the event even if reject() cancels.
    // Instead, ignore the event and route through reject() which handles save prompt.
    event->ignore();
    reject();
}

void StandaloneFileTinderDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    
    // Use debounced timer to update preview after resize stops
    // This prevents stutter during continuous resizing
    if (resize_timer_) {
        resize_timer_->start();  // Restart the timer on each resize event
    }
}

void StandaloneFileTinderDialog::reject() {
    // Guard against re-entrant calls (QMessageBox can trigger events)
    if (closing_) {
        QDialog::reject();
        return;
    }
    
    // Check if there are any pending decisions to save
    int reviewed = keep_count_ + delete_count_ + sort_later_count_ + move_count_;
    
    if (reviewed > 0 && !files_.empty()) {
        closing_ = true;  // Prevent re-entry from message box events
        
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            "Save Progress?",
            QString("You have made %1 decisions. Do you want to save your progress before closing?\n\n"
                    "Your session will be saved and you can continue later.")
                    .arg(reviewed),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save
        );
        
        if (reply == QMessageBox::Save) {
            save_session_state();
            QDialog::reject();
        } else if (reply == QMessageBox::Discard) {
            QDialog::reject();
        } else {
            // Cancel — don't close
            closing_ = false;
        }
    } else {
        // No decisions made, just save state and close
        save_session_state();
        QDialog::reject();
    }
}

bool StandaloneFileTinderDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == file_info_label_ && event->type() == QEvent::MouseButtonDblClick) {
        int file_idx = get_current_file_index();
        if (file_idx >= 0 && file_idx < static_cast<int>(files_.size())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(files_[file_idx].path));
        }
        return true;
    }
    if (obj == file_info_label_ && event->type() == QEvent::ContextMenu) {
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
    return QDialog::eventFilter(obj, event);
}

void StandaloneFileTinderDialog::on_switch_mode_clicked() {
    QMenu menu(this);
    auto* adv_action = menu.addAction("Advanced Mode");
    auto* ai_action = menu.addAction("AI Mode");
    QAction* selected = menu.exec(switch_mode_btn_->mapToGlobal(
        QPoint(0, switch_mode_btn_->height())));
    if (selected == adv_action) {
        save_session_state();
        emit switch_to_advanced_mode();
    } else if (selected == ai_action) {
        save_session_state();
        emit switch_to_ai_mode();
    }
}

// Sorting implementation
void StandaloneFileTinderDialog::on_sort_changed(int index) {
    if (!sort_combo_) return;
    sort_field_ = static_cast<FileSortField>(sort_combo_->itemData(index).toInt());
    apply_sort();
    rebuild_filtered_indices();
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    update_progress();
}

void StandaloneFileTinderDialog::on_sort_order_toggled() {
    if (sort_order_ == SortOrder::Ascending) {
        sort_order_ = SortOrder::Descending;
        sort_order_btn_->setText("Desc");
    } else {
        sort_order_ = SortOrder::Ascending;
        sort_order_btn_->setText("Asc");
    }
    apply_sort();
    rebuild_filtered_indices();
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    update_progress();
}

void StandaloneFileTinderDialog::on_folders_toggle_changed(int state) {
    include_folders_ = (state == Qt::Checked);
    // Reset counts before re-scanning and reloading state
    keep_count_ = 0;
    delete_count_ = 0;
    sort_later_count_ = 0;
    move_count_ = 0;
    scan_files();  // Re-scan with new settings
    apply_sort();
    rebuild_filtered_indices();
    load_session_state();
    if (!filtered_indices_.empty()) {
        show_current_file();
    }
    update_progress();
}

void StandaloneFileTinderDialog::apply_sort() {
    if (files_.empty()) return;
    
    auto compare_fn = [this](const FileToProcess& a, const FileToProcess& b) {
        // When including folders, always sort directories before files
        if (include_folders_ && a.is_directory != b.is_directory) {
            return a.is_directory;  // directories come first
        }
        int cmp = 0;
        switch (sort_field_) {
            case FileSortField::Name:
                cmp = a.name.compare(b.name, Qt::CaseInsensitive);
                break;
            case FileSortField::Size:
                cmp = (a.size < b.size) ? -1 : (a.size > b.size) ? 1 : 0;
                break;
            case FileSortField::Type:
                cmp = a.extension.compare(b.extension, Qt::CaseInsensitive);
                break;
            case FileSortField::DateModified:
                cmp = (a.modified_datetime < b.modified_datetime) ? -1 : 
                      (a.modified_datetime > b.modified_datetime) ? 1 : 0;
                break;
        }
        return sort_order_ == SortOrder::Ascending ? cmp < 0 : cmp > 0;
    };
    
    std::sort(files_.begin(), files_.end(), compare_fn);
}

void StandaloneFileTinderDialog::show_custom_extension_dialog() {
    bool ok;
    QString text = QInputDialog::getText(this, "Custom Filter",
        "Enter file extensions separated by commas (e.g., txt,csv,log):",
        QLineEdit::Normal, custom_extensions_.join(","), &ok);
    
    if (ok && !text.isEmpty()) {
        custom_extensions_.clear();
        QStringList parts = text.split(",", Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            QString ext = part.trimmed().toLower();
            if (!ext.startsWith(".")) ext = "." + ext;
            custom_extensions_.append(ext);
        }
        rebuild_filtered_indices();
        if (!filtered_indices_.empty()) {
            show_current_file();
        } else {
            if (preview_label_)
                preview_label_->setText("No files match the specified extensions");
            if (file_info_label_)
                file_info_label_->setText("Extensions: " + custom_extensions_.join(", "));
        }
        update_progress();
    }
}

// Filter implementation
void StandaloneFileTinderDialog::on_filter_changed(int index) {
    FileFilterType filter = static_cast<FileFilterType>(filter_combo_->itemData(index).toInt());
    
    // Handle custom filter specially
    if (filter == FileFilterType::Custom) {
        show_custom_extension_dialog();
        return;
    }
    
    apply_filter(filter);
}

void StandaloneFileTinderDialog::apply_filter(FileFilterType filter) {
    current_filter_ = filter;
    
    // Prompt user about resetting progress when filter changes
    int reviewed = keep_count_ + delete_count_ + sort_later_count_ + move_count_;
    if (reviewed > 0) {
        auto reply = QMessageBox::question(this, "Filter Changed",
            QString("You have %1 decisions made. Do you want to reset progress and start fresh with this filter?\n\n"
                    "• Yes — clear all decisions\n"
                    "• No — keep existing decisions").arg(reviewed),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            // Reset all decisions to pending
            for (auto& file : files_) {
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
    
    rebuild_filtered_indices();
    
    // Stay on current file if it matches the new filter, otherwise find first pending
    int prev_file_idx = get_current_file_index();
    bool found_current = false;
    if (prev_file_idx >= 0) {
        for (size_t i = 0; i < filtered_indices_.size(); ++i) {
            if (filtered_indices_[i] == prev_file_idx) {
                current_filtered_index_ = static_cast<int>(i);
                found_current = true;
                break;
            }
        }
    }
    if (!found_current) {
        current_filtered_index_ = 0;
        for (size_t i = 0; i < filtered_indices_.size(); ++i) {
            if (files_[filtered_indices_[i]].decision == "pending") {
                current_filtered_index_ = static_cast<int>(i);
                break;
            }
        }
    }
    
    if (!filtered_indices_.empty()) {
        show_current_file();
    } else {
        if (preview_label_)
            preview_label_->setText("<div style='text-align: center; font-size: 24px; color: #f39c12;'>"
                                   "No files match this filter</div>");
        if (file_info_label_)
            file_info_label_->setText("Try selecting a different filter or 'All Files'.");
    }
    
    update_progress();
}

void StandaloneFileTinderDialog::rebuild_filtered_indices() {
    filtered_indices_.clear();
    
    for (size_t i = 0; i < files_.size(); ++i) {
        if (file_matches_filter(files_[i])) {
            filtered_indices_.push_back(static_cast<int>(i));
        }
    }
}

bool StandaloneFileTinderDialog::file_matches_filter(const FileToProcess& file) const {
    if (current_filter_ == FileFilterType::All) {
        return true;
    }
    
    QString mime = file.mime_type.toLower();
    
    switch (current_filter_) {
        case FileFilterType::Images:
            return !file.is_directory && mime.startsWith("image/");
        case FileFilterType::Videos:
            return !file.is_directory && mime.startsWith("video/");
        case FileFilterType::Audio:
            return !file.is_directory && mime.startsWith("audio/");
        case FileFilterType::Documents:
            return !file.is_directory && (mime.startsWith("text/") || 
                   mime.contains("pdf") || 
                   mime.contains("document") ||
                   mime.contains("spreadsheet") ||
                   mime.contains("presentation"));
        case FileFilterType::Archives:
            return !file.is_directory && (mime.contains("zip") || 
                   mime.contains("tar") || 
                   mime.contains("archive") ||
                   mime.contains("compressed"));
        case FileFilterType::Other:
            // Match anything not covered by other filters (excluding directories)
            return !file.is_directory &&
                   !mime.startsWith("image/") && 
                   !mime.startsWith("video/") && 
                   !mime.startsWith("audio/") &&
                   !mime.startsWith("text/") &&
                   !mime.contains("pdf") &&
                   !mime.contains("document") &&
                   !mime.contains("spreadsheet") &&
                   !mime.contains("presentation") &&
                   !mime.contains("zip") &&
                   !mime.contains("archive");
        case FileFilterType::FoldersOnly:
            return file.is_directory;
        case FileFilterType::Custom: {
            // Match custom extensions
            if (custom_extensions_.isEmpty()) return true;
            QString ext = "." + file.extension.toLower();
            for (const QString& custom_ext : custom_extensions_) {
                if (ext == custom_ext || file.extension.toLower() == custom_ext.mid(1)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return true;
    }
}

// Animation implementation
void StandaloneFileTinderDialog::animate_swipe(bool forward) {
    if (!preview_label_) return;
    
    animating_ = true;
    
    // Reuse existing effect or create new one
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(preview_label_->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(preview_label_);
        preview_label_->setGraphicsEffect(effect);
    }
    
    // If animation is running, stop it and reset opacity
    if (swipe_animation_) {
        if (swipe_animation_->state() == QAbstractAnimation::Running) {
            swipe_animation_->stop();  // triggers DeleteWhenStopped
            effect->setOpacity(1.0);
        } else {
            // Animation exists but not running (finished handler may not have fired yet)
            delete swipe_animation_;
        }
        swipe_animation_ = nullptr;
    }
    
    // Create new animation owned by 'this' for safe lifetime
    swipe_animation_ = new QPropertyAnimation(effect, "opacity", this);
    swipe_animation_->setDuration(150);
    
    if (forward) {
        swipe_animation_->setStartValue(1.0);
        swipe_animation_->setEndValue(0.3);
    } else {
        swipe_animation_->setStartValue(0.3);
        swipe_animation_->setEndValue(1.0);
    }
    
    swipe_animation_->setEasingCurve(QEasingCurve::OutQuad);
    
    // Use QPointer to safely access effect in callback
    QPointer<QGraphicsOpacityEffect> weak_effect = effect;
    QPointer<StandaloneFileTinderDialog> weak_this = this;
    connect(swipe_animation_, &QPropertyAnimation::finished, this, [weak_effect, weak_this]() {
        if (weak_effect) {
            weak_effect->setOpacity(1.0);
        }
        if (weak_this) {
            weak_this->swipe_animation_ = nullptr;
            weak_this->animating_ = false;
        }
    });
    
    swipe_animation_->start(QAbstractAnimation::DeleteWhenStopped);
}

QStringList StandaloneFileTinderDialog::get_destination_folders() const {
    return {};  // Base class has no grid folders
}

// Shortcuts help dialog
void StandaloneFileTinderDialog::show_shortcuts_help() {
    QMessageBox help_dialog(this);
    help_dialog.setWindowTitle("Keyboard Shortcuts");
    help_dialog.setIcon(QMessageBox::Information);
    
    QString shortcuts = R"(
<style>
    table { border-collapse: collapse; width: 100%; }
    th, td { padding: 6px; text-align: left; border-bottom: 1px solid #444; }
    th { background-color: #34495e; color: white; }
    .key { font-family: monospace; background: #3d566e; padding: 2px 6px; border-radius: 3px; }
    .section { background-color: #2c3e50; color: #3498db; font-weight: bold; }
</style>
<table>
<tr><th>Key</th><th>Action</th></tr>
<tr class='section'><td colspan='2'>All Modes</td></tr>
<tr><td><span class='key'>&rarr;</span> Right Arrow</td><td>Keep file in original location</td></tr>
<tr><td><span class='key'>&larr;</span> Left Arrow</td><td>Mark file for deletion</td></tr>
<tr><td><span class='key'>&darr;</span> Down Arrow</td><td>Sort later (move to end of list)</td></tr>
<tr><td><span class='key'>Z</span> or <span class='key'>Backspace</span></td><td>Undo last action</td></tr>
<tr><td><span class='key'>Y</span></td><td>Redo last undone action</td></tr>
<tr><td><span class='key'>P</span></td><td>Toggle file preview</td></tr>
<tr><td><span class='key'>Enter</span></td><td>Finish review and execute</td></tr>
<tr><td><span class='key'>?</span> or <span class='key'>Shift+/</span></td><td>Show this help</td></tr>
<tr><td><span class='key'>Esc</span></td><td>Close dialog</td></tr>
<tr><td>File List button</td><td>Open file list window (multi-select, right-click to assign)</td></tr>
<tr><td><span class='key'>F</span></td><td>Open File List window (multi-select, filter, assign)</td></tr>
<tr><td><span class='key'>Ctrl+F</span></td><td>Focus search box</td></tr>
<tr><td><span class='key'>!</span> button</td><td>View and manage duplicate files (when detected)</td></tr>
<tr class='section'><td colspan='2'>Advanced / AI Mode</td></tr>
<tr><td><span class='key'>K</span></td><td>Keep file</td></tr>
<tr><td><span class='key'>1</span>-<span class='key'>0</span></td><td>Move file to Quick Access folder 1-10</td></tr>
<tr><td>Click grid folder</td><td>Move file to that folder</td></tr>
<tr><td><span class='key'>Tab</span></td><td>Enter grid keyboard navigation (arrows to move, Space to assign, Esc to exit)</td></tr>
<tr><td><span class='key'>N</span></td><td>Add new folder to grid</td></tr>
<tr><td>A-Z / # buttons</td><td>Sort grid alphabetically / by file count</td></tr>
<tr><td>Full Paths toggle</td><td>Show folder path structure in grid buttons</td></tr>
<tr><td>Width spinner</td><td>Adjust grid button width (80-300px)</td></tr>
<tr class='section'><td colspan='2'>AI Mode</td></tr>
<tr><td>AI Setup button</td><td>Configure provider, model, and sorting options</td></tr>
<tr><td>Re-run AI button</td><td>Re-analyze unsorted or all files</td></tr>
<tr><td>Semi mode</td><td>AI highlights suggested folders in grid + AI Suggestions bar</td></tr>
<tr><td>AI Reasoning</td><td>Shows why the AI picked specific folders for the current file</td></tr>
<tr><td>Confidence %</td><td>Color-coded certainty level on each suggestion</td></tr>
<tr><td>Category review</td><td>Edit AI-proposed categories before they are created</td></tr>
</table>
<br>
<b>Tip:</b> Use the File List to browse and multi-select files. In the review screen, type folder paths directly into the destination dropdown. Right-click folders for context menu. Use A-Z/#  to organize the grid. Switch themes from the launcher.
)";
    
    help_dialog.setTextFormat(Qt::RichText);
    help_dialog.setText(shortcuts);
    help_dialog.exec();
}

void StandaloneFileTinderDialog::on_search(const QString& text) {
    if (text.isEmpty()) return;
    
    int count = static_cast<int>(filtered_indices_.size());
    if (count == 0) return;
    
    // Search forward from the position after the current one, wrapping around
    int start = (current_filtered_index_ + 1) % count;
    for (int offset = 0; offset < count; ++offset) {
        int i = (start + offset) % count;
        const auto& file = files_[filtered_indices_[i]];
        if (file.name.contains(text, Qt::CaseInsensitive)) {
            current_filtered_index_ = i;
            show_current_file();
            return;
        }
    }
}
