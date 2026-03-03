#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QMessageBox>
#include <QStyleFactory>
#include <QDir>
#include <QSettings>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileInfo>
#include <QMouseEvent>
#include <QTimer>
#include <QMimeDatabase>
#include <QProgressDialog>
#include <QStackedWidget>

#include "DatabaseManager.hpp"
#include "StandaloneFileTinderDialog.hpp"
#include "AdvancedFileTinderDialog.hpp"
#include "AiFileTinderDialog.hpp"
#include "FileTinderExecutor.hpp"
#include "AppLogger.hpp"
#include "ui_constants.hpp"
#include "UserDataDialog.hpp"

class FileTinderLauncher : public QWidget {
    Q_OBJECT
    
public:
    FileTinderLauncher(QWidget* parent_widget = nullptr) 
        : QWidget(parent_widget)
        , db_manager_()
        , chosen_path_() {
        
        setWindowTitle("File Tinder");
        setMinimumSize(ui::scaling::scaled(550), ui::scaling::scaled(450));
        
        LOG_INFO("Launcher", "Application starting");
        
        if (!db_manager_.initialize()) {
            LOG_ERROR("Launcher", "Database initialization failed");
            QMessageBox::critical(this, "Database Error", "Could not initialize the database.");
        } else {
            // Auto-cleanup sessions older than 30 days
            int cleaned = db_manager_.cleanup_stale_sessions(30);
            if (cleaned > 0) {
                LOG_INFO("Launcher", QString("Cleaned %1 stale session(s)").arg(cleaned));
            }
        }
        
        // Set up the stacked widget architecture
        stack_ = new QStackedWidget(this);
        launcher_page_ = new QWidget();
        build_interface();
        stack_->addWidget(launcher_page_);  // index 0
        
        auto* main_layout = new QVBoxLayout(this);
        main_layout->setContentsMargins(0, 0, 0, 0);
        main_layout->addWidget(stack_);
        
        // Load theme preference
        QSettings theme_settings("FileTinder", "FileTinder");
        is_dark_theme_ = theme_settings.value("darkTheme", true).toBool();
        apply_theme();
        
        // Pre-fill last used folder if it still exists
        QSettings settings("FileTinder", "FileTinder");
        QString last_folder = settings.value("lastFolder").toString();
        if (!last_folder.isEmpty() && QDir(last_folder).exists()) {
            chosen_path_ = last_folder;
            // Restore additional source folders from DB, fall back to primary folder
            QStringList saved_folders = db_manager_.get_source_folders(last_folder);
            source_folders_ = saved_folders.isEmpty() ? QStringList{last_folder} : saved_folders;
            
            path_indicator_->setText(last_folder);
            path_indicator_->setStyleSheet(
                "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
            );
            
            // Check for resumable session
            check_session_state();
        }
    }
    
private:
    DatabaseManager db_manager_;
    QString chosen_path_;
    QStringList source_folders_;
    QLabel* path_indicator_;
    QListWidget* recent_list_ = nullptr;
    QListWidget* multi_folder_list_ = nullptr;
    QCheckBox* multi_folder_check_ = nullptr;
    QPushButton* add_folder_btn_ = nullptr;
    QLabel* resume_label_ = nullptr;
    bool skip_stats_on_next_launch_ = false;  // Skip stats dashboard on mode switch
    bool is_dark_theme_ = true;
    
    // Stacked widget architecture
    QStackedWidget* stack_ = nullptr;
    QWidget* launcher_page_ = nullptr;
    StandaloneFileTinderDialog* basic_widget_ = nullptr;
    AdvancedFileTinderDialog* advanced_widget_ = nullptr;
    AiFileTinderDialog* ai_widget_ = nullptr;
    QSize launcher_size_;  // Saved launcher page size for restoring on return
    
    // Resize the window appropriately for the given mode, keeping it on screen
    void resize_for_mode(const QString& mode) {
        auto* screen = QApplication::primaryScreen();
        if (!screen) return;
        QRect avail = screen->availableGeometry();
        int w, h;
        if (mode == "basic") {
            w = qMin(ui::scaling::scaled(ui::dimensions::kStandaloneFileTinderMinWidth),
                      avail.width() * 85 / 100);
            h = qMin(ui::scaling::scaled(ui::dimensions::kStandaloneFileTinderMinHeight),
                      avail.height() * 75 / 100);
        } else {
            // Advanced and AI modes need more space
            w = qMin(ui::scaling::scaled(ui::dimensions::kAdvancedFileTinderMinWidth),
                      avail.width() * 9 / 10);
            h = qMin(ui::scaling::scaled(ui::dimensions::kAdvancedFileTinderMinHeight),
                      avail.height() * 8 / 10);
        }
        resize(w, h);
        ensure_on_screen();
    }
    
    // Ensure the window stays fully within the available screen area
    void ensure_on_screen() {
        auto* screen = QApplication::primaryScreen();
        if (!screen) return;
        QRect avail = screen->availableGeometry();
        QRect geo = geometry();
        
        // Clamp width/height to screen
        if (geo.width() > avail.width())
            geo.setWidth(avail.width());
        if (geo.height() > avail.height())
            geo.setHeight(avail.height());
        
        // Keep the window within screen bounds
        if (geo.left() < avail.left())
            geo.moveLeft(avail.left());
        if (geo.top() < avail.top())
            geo.moveTop(avail.top());
        if (geo.right() > avail.right())
            geo.moveRight(avail.right());
        if (geo.bottom() > avail.bottom())
            geo.moveBottom(avail.bottom());
        
        setGeometry(geo);
    }
    
    void apply_theme() {
        QPalette p;
        if (is_dark_theme_) {
            p.setColor(QPalette::Window, QColor(45, 45, 45));
            p.setColor(QPalette::WindowText, QColor(230, 230, 230));
            p.setColor(QPalette::Base, QColor(35, 35, 35));
            p.setColor(QPalette::AlternateBase, QColor(55, 55, 55));
            p.setColor(QPalette::Text, QColor(230, 230, 230));
            p.setColor(QPalette::Button, QColor(50, 50, 50));
            p.setColor(QPalette::ButtonText, QColor(230, 230, 230));
            p.setColor(QPalette::Highlight, QColor(0, 120, 212));
            p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        } else {
            p.setColor(QPalette::Window, QColor(248, 249, 250));
            p.setColor(QPalette::WindowText, QColor(33, 37, 41));
            p.setColor(QPalette::Base, QColor(255, 255, 255));
            p.setColor(QPalette::AlternateBase, QColor(241, 243, 245));
            p.setColor(QPalette::Text, QColor(33, 37, 41));
            p.setColor(QPalette::Button, QColor(233, 236, 239));
            p.setColor(QPalette::ButtonText, QColor(33, 37, 41));
            p.setColor(QPalette::Highlight, QColor(13, 110, 253));
            p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
            p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
            p.setColor(QPalette::ToolTipText, QColor(33, 37, 41));
            p.setColor(QPalette::PlaceholderText, QColor(108, 117, 125));
            p.setColor(QPalette::Link, QColor(13, 110, 253));
            p.setColor(QPalette::Disabled, QPalette::Text, QColor(173, 181, 189));
            p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(173, 181, 189));
        }
        QApplication::setPalette(p);
        if (is_dark_theme_) {
            qApp->setStyleSheet(
                "QToolTip { background-color: #2d2d2d; color: #e6e6e6; border: 1px solid #555; }"
            );
        } else {
            qApp->setStyleSheet(
                "QToolTip { background-color: #f5f5f5; color: #333; border: 1px solid #ccc; }"
            );
        }
    }
    
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (recent_list_ && obj == recent_list_->viewport() && event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::MiddleButton) {
                auto* item = recent_list_->itemAt(me->pos());
                if (item) {
                    QString path = item->text();
                    delete recent_list_->takeItem(recent_list_->row(item));
                    db_manager_.remove_recent_folder(path);
                    LOG_INFO("Launcher", QString("Removed recent folder: %1").arg(path));
                    return true;
                }
            }
        }
        return QWidget::eventFilter(obj, event);
    }
    
    void build_interface() {
        auto* root_layout = new QVBoxLayout(launcher_page_);
        root_layout->setContentsMargins(25, 25, 25, 25);
        root_layout->setSpacing(18);
        
        // App header
        auto* app_title = new QLabel("FILE TINDER");
        app_title->setStyleSheet("font-size: 28px; font-weight: bold; color: #0078d4;");
        app_title->setAlignment(Qt::AlignCenter);
        root_layout->addWidget(app_title);
        
        auto* app_desc = new QLabel("Organize files with swipe-style sorting");
        app_desc->setStyleSheet("font-size: 13px; color: #888888;");
        app_desc->setAlignment(Qt::AlignCenter);
        root_layout->addWidget(app_desc);
        
        root_layout->addSpacing(15);
        
        // Folder picker section
        auto* picker_label = new QLabel("Select Folder to Organize");
        picker_label->setStyleSheet("font-weight: bold; font-size: 12px;");
        root_layout->addWidget(picker_label);
        
        auto* picker_row = new QHBoxLayout();
        
        path_indicator_ = new QLabel("(none selected)");
        path_indicator_->setStyleSheet(
            "padding: 8px 12px; background-color: #2d2d2d; border: 1px solid #404040; color: #aaaaaa;"
        );
        path_indicator_->setWordWrap(true);
        picker_row->addWidget(path_indicator_, 1);
        
        auto* pick_btn = new QPushButton("Select...");
        pick_btn->setStyleSheet(
            "QPushButton { padding: 8px 16px; background-color: #0078d4; color: white; border: none; }"
            "QPushButton:hover { background-color: #106ebe; }"
        );
        connect(pick_btn, &QPushButton::clicked, this, &FileTinderLauncher::pick_folder);
        picker_row->addWidget(pick_btn);
        
        root_layout->addLayout(picker_row);
        
        // Multiple folders checkbox
        multi_folder_check_ = new QCheckBox("Multiple Folders");
        multi_folder_check_->setStyleSheet("color: #aaaaaa; font-size: 11px;");
        multi_folder_check_->setToolTip("Enable to scan files from multiple source folders");
        root_layout->addWidget(multi_folder_check_);
        
        // Multi-folder list (hidden by default)
        multi_folder_list_ = new QListWidget();
        multi_folder_list_->setMaximumHeight(ui::scaling::scaled(80));
        multi_folder_list_->setStyleSheet(
            "QListWidget { background-color: #2d2d2d; border: 1px solid #404040; color: #aaaaaa; }"
            "QListWidget::item { padding: 3px 8px; }"
            "QListWidget::item:hover { background-color: #3a3a3a; }"
            "QListWidget::item:selected { background-color: #0078d4; color: white; }"
        );
        multi_folder_list_->setContextMenuPolicy(Qt::CustomContextMenu);
        multi_folder_list_->setVisible(false);
        connect(multi_folder_list_, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint& pos) {
            auto* item = multi_folder_list_->itemAt(pos);
            if (!item) return;
            QMenu menu(this);
            auto* remove_action = menu.addAction("Remove");
            if (menu.exec(multi_folder_list_->mapToGlobal(pos)) == remove_action) {
                // Prevent removing the last folder
                if (source_folders_.size() <= 1) return;
                QString path = item->text();
                delete multi_folder_list_->takeItem(multi_folder_list_->row(item));
                source_folders_.removeAll(path);
                update_multi_folder_display();
            }
        });
        root_layout->addWidget(multi_folder_list_);
        
        // Add Folder button (hidden by default)
        add_folder_btn_ = new QPushButton("Add Folder...");
        add_folder_btn_->setStyleSheet(
            "QPushButton { padding: 4px 12px; background-color: #3a3a3a; color: #aaaaaa; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #4a4a4a; }"
        );
        add_folder_btn_->setVisible(false);
        connect(add_folder_btn_, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getExistingDirectory(this, "Add Source Folder", QDir::homePath());
            if (!path.isEmpty() && !source_folders_.contains(path)) {
                source_folders_.append(path);
                update_multi_folder_display();
            }
        });
        root_layout->addWidget(add_folder_btn_);
        
        connect(multi_folder_check_, &QCheckBox::toggled, this, [this](bool checked) {
            add_folder_btn_->setVisible(checked);
            if (checked) {
                // Seed source_folders_ with current chosen_path_ if not empty
                if (!chosen_path_.isEmpty() && !source_folders_.contains(chosen_path_)) {
                    source_folders_.prepend(chosen_path_);
                }
            } else {
                // Revert to single-folder mode: keep only primary
                if (!source_folders_.isEmpty()) {
                    chosen_path_ = source_folders_.first();
                }
                source_folders_.clear();
                if (!chosen_path_.isEmpty()) {
                    source_folders_.append(chosen_path_);
                }
            }
            update_multi_folder_display();
        });
        
        // Recent folders list (middle-click to remove)
        QStringList recent = db_manager_.get_recent_folders(5);
        if (!recent.isEmpty()) {
            auto* recent_label = new QLabel("Recent Folders (click to select, middle-click to remove):");
            recent_label->setStyleSheet("color: #888888; font-size: 10px;");
            root_layout->addWidget(recent_label);
            
            auto* recent_list = new QListWidget();
            recent_list->setMaximumHeight(ui::scaling::scaled(80));
            recent_list->setStyleSheet(
                "QListWidget { background-color: #2d2d2d; border: 1px solid #404040; color: #aaaaaa; }"
                "QListWidget::item { padding: 3px 8px; }"
                "QListWidget::item:hover { background-color: #3a3a3a; }"
                "QListWidget::item:selected { background-color: #0078d4; color: white; }"
            );
            for (const QString& folder : recent) {
                recent_list->addItem(folder);
            }
            
            connect(recent_list, &QListWidget::itemClicked, this,
                    [this, recent_list](QListWidgetItem* item) {
                QString path = item->text();
                if (QDir(path).exists()) {
                    if (multi_folder_check_ && multi_folder_check_->isChecked()) {
                        // In multi-folder mode, add to source list
                        if (!source_folders_.contains(path)) {
                            source_folders_.append(path);
                            update_multi_folder_display();
                        }
                    } else {
                        chosen_path_ = path;
                        source_folders_ = {path};
                        path_indicator_->setText(path);
                        path_indicator_->setStyleSheet(
                            "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
                        );
                    }
                } else {
                    QMessageBox::warning(this, "Folder Not Found",
                        QString("The folder no longer exists:\n%1").arg(path));
                }
                recent_list->clearSelection();
            });
            
            // Middle-click to remove
            recent_list->viewport()->installEventFilter(this);
            recent_list_ = recent_list;
            recent_list_->setToolTip("Click to select. Middle-click to remove.");
            root_layout->addWidget(recent_list);
        }
        
        root_layout->addStretch();
        
        // Resume session indicator
        resume_label_ = new QLabel();
        resume_label_->setStyleSheet(
            "padding: 8px 12px; background-color: #1a3a4a; border: 1px solid #2980b9; "
            "color: #3498db; font-size: 11px; border-radius: 4px;");
        resume_label_->setVisible(false);
        root_layout->addWidget(resume_label_);
        
        // Mode buttons
        auto* modes_label = new QLabel("Sorting Mode");
        modes_label->setStyleSheet("font-weight: bold; font-size: 12px;");
        root_layout->addWidget(modes_label);
        
        auto* modes_row = new QHBoxLayout();
        modes_row->setSpacing(12);
        
        auto* basic_mode_btn = new QPushButton("Basic Mode\n(Keep / Delete / Sort Later)");
        basic_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        basic_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #107c10; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #0e6b0e; }"
        );
        connect(basic_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_basic);
        modes_row->addWidget(basic_mode_btn);
        
        auto* adv_mode_btn = new QPushButton("Advanced Mode\n(Folder grid + assignment)");
        adv_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        adv_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #5c2d91; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #4a2473; }"
        );
        connect(adv_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_advanced);
        modes_row->addWidget(adv_mode_btn);
        
        auto* ai_mode_btn = new QPushButton("AI Mode\n(Auto-sort with AI suggestions)");
        ai_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        ai_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #2980b9; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #1a6fa0; }"
        );
        connect(ai_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_ai);
        modes_row->addWidget(ai_mode_btn);
        
        root_layout->addLayout(modes_row);
        
        // Tools row: Clear Session + Undo History + User Data
        auto* tools_row = new QHBoxLayout();
        
        auto* clear_btn = new QPushButton("Clear Session");
        clear_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(clear_btn, &QPushButton::clicked, this, &FileTinderLauncher::clear_session);
        tools_row->addWidget(clear_btn);
        
        auto* undo_history_btn = new QPushButton("Undo History");
        undo_history_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(undo_history_btn, &QPushButton::clicked, this, &FileTinderLauncher::show_undo_history);
        tools_row->addWidget(undo_history_btn);
        
        tools_row->addStretch();
        
        // Theme toggle button
        auto* theme_btn = new QPushButton("Light Theme");
        theme_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(theme_btn, &QPushButton::clicked, this, [this, theme_btn]() {
            is_dark_theme_ = !is_dark_theme_;
            apply_theme();
            theme_btn->setText(is_dark_theme_ ? "Light Theme" : "Dark Theme");
            QSettings settings("FileTinder", "FileTinder");
            settings.setValue("darkTheme", is_dark_theme_);
        });
        tools_row->addWidget(theme_btn);
        
        auto* user_data_btn = new QPushButton("User Data");
        user_data_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(user_data_btn, &QPushButton::clicked, this, [this]() {
            UserDataDialog dialog(db_manager_, this);
            dialog.exec();
        });
        tools_row->addWidget(user_data_btn);
        
        root_layout->addLayout(tools_row);
        
        // Hotkey hint
        auto* hint_text = new QLabel(
            "Keys: Left=Delete | Down=Sort Later | Z=Undo | Y=Redo | F=File List | P=Preview | ?=Help | Enter=Review\n"
            "Basic: Right=Keep | Advanced/AI: K=Keep, 1-0=Quick Access, Tab=Grid Nav"
        );
        hint_text->setStyleSheet("color: #666666; font-size: 10px; padding-top: 8px;");
        hint_text->setAlignment(Qt::AlignCenter);
        hint_text->setWordWrap(true);
        root_layout->addWidget(hint_text);
    }
    
    void pick_folder() {
        QString path = QFileDialog::getExistingDirectory(this, "Pick Folder", QDir::homePath());
        if (!path.isEmpty()) {
            if (multi_folder_check_ && multi_folder_check_->isChecked()) {
                // In multi-folder mode, add as source; set as primary if first
                if (!source_folders_.contains(path)) {
                    if (source_folders_.isEmpty()) {
                        chosen_path_ = path;
                    }
                    source_folders_.append(path);
                    update_multi_folder_display();
                }
            } else {
                chosen_path_ = path;
                source_folders_ = {path};
                path_indicator_->setText(path);
                path_indicator_->setStyleSheet(
                    "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
                );
            }
            LOG_INFO("Launcher", QString("Folder selected: %1").arg(path));
        }
    }
    
    void update_multi_folder_display() {
        bool multi = multi_folder_check_ && multi_folder_check_->isChecked();
        bool show_list = multi && source_folders_.size() >= 2;
        if (multi_folder_list_) multi_folder_list_->setVisible(show_list);
        
        if (multi_folder_list_ && show_list) {
            multi_folder_list_->clear();
            for (const QString& f : source_folders_) {
                multi_folder_list_->addItem(f);
            }
        }
        
        // Update chosen_path_ to the first source folder
        if (!source_folders_.isEmpty()) {
            chosen_path_ = source_folders_.first();
        }
        
        // Update path indicator
        if (multi && source_folders_.size() > 1) {
            path_indicator_->setText(QString("%1 folders selected").arg(source_folders_.size()));
            path_indicator_->setStyleSheet(
                "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
            );
        } else if (!chosen_path_.isEmpty()) {
            path_indicator_->setText(chosen_path_);
            path_indicator_->setStyleSheet(
                "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
            );
        }
    }
    
    bool show_pre_session_stats() {
        // Collect files from all source folders
        QStringList all_files;
        QStringList subfolders;
        for (const QString& src : source_folders_) {
            QDir dir(src);
            if (!dir.exists()) continue;
            QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QString& f : entries) {
                all_files.append(dir.absoluteFilePath(f));
            }
            QStringList sub = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& s : sub) {
                subfolders.append(s);
            }
        }
        
        if (all_files.isEmpty()) {
            QMessageBox::information(this, "Empty Folder", "The selected folders have no files to sort.");
            return false;
        }
        
        // Collect file stats (show progress for large dirs)
        qint64 total_size = 0;
        int img_count = 0, vid_count = 0, aud_count = 0, doc_count = 0, arch_count = 0, other_count = 0;
        QMimeDatabase mime_db;
        
        QProgressDialog* progress = nullptr;
        if (all_files.size() > 200) {
            progress = new QProgressDialog("Analyzing files...", QString(), 0, all_files.size(), this);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->show();
        }
        
        for (int i = 0; i < all_files.size(); ++i) {
            QFileInfo info(all_files[i]);
            total_size += info.size();
            QString mime = mime_db.mimeTypeForFile(info.absoluteFilePath()).name();
            if (mime.startsWith("image/")) img_count++;
            else if (mime.startsWith("video/")) vid_count++;
            else if (mime.startsWith("audio/")) aud_count++;
            else if (mime.startsWith("text/") || mime.contains("pdf") || mime.contains("document")) doc_count++;
            else if (mime.contains("zip") || mime.contains("archive") || mime.contains("compressed")) arch_count++;
            else other_count++;
            
            if (progress && (i % 50 == 0)) {
                progress->setValue(i);
                QApplication::processEvents();
            }
        }
        delete progress;
        
        int folder_count = subfolders.size();
        
        // Format size
        QString size_str;
        if (total_size < 1024LL) size_str = QString("%1 B").arg(total_size);
        else if (total_size < 1024LL*1024) size_str = QString("%1 KB").arg(total_size/1024.0, 0, 'f', 1);
        else if (total_size < 1024LL*1024*1024) size_str = QString("%1 MB").arg(total_size/(1024.0*1024.0), 0, 'f', 1);
        else size_str = QString("%1 GB").arg(total_size/(1024.0*1024.0*1024.0), 0, 'f', 2);
        
        // Show dashboard
        QDialog dashboard(this);
        dashboard.setWindowTitle("Session Overview");
        dashboard.setMinimumSize(ui::scaling::scaled(400), ui::scaling::scaled(250));
        
        auto* layout = new QVBoxLayout(&dashboard);
        layout->setContentsMargins(15, 12, 15, 12);
        layout->setSpacing(8);
        
        QString header_text = source_folders_.size() > 1
            ? QString("%1 source folders (primary: %2)").arg(source_folders_.size()).arg(chosen_path_)
            : chosen_path_;
        auto* header = new QLabel(header_text);
        header->setStyleSheet("font-size: 13px; font-weight: bold; color: #3498db;");
        header->setWordWrap(true);
        layout->addWidget(header);
        
        auto* summary = new QLabel(QString(
            "<span style='font-size: 13px;'>"
            "<b>%1 files</b> &middot; %2 total &middot; <b>%3 subfolders</b>"
            "</span>"
        ).arg(all_files.size()).arg(size_str).arg(folder_count));
        layout->addWidget(summary);
        
        // Type breakdown
        auto* breakdown = new QWidget();
        breakdown->setStyleSheet("background-color: #34495e; border-radius: 6px; padding: 8px;");
        auto* bd_layout = new QVBoxLayout(breakdown);
        bd_layout->setContentsMargins(8, 6, 8, 6);
        bd_layout->setSpacing(2);
        
        auto add_row = [&](const QString& label, int count, const QString& color) {
            if (count == 0) return;
            auto* row = new QLabel(QString(
                "<span style='color: %2; font-size: 13px;'>%1: <b>%3</b></span>"
            ).arg(label, color).arg(count));
            bd_layout->addWidget(row);
        };
        
        add_row("Images", img_count, "#3498db");
        add_row("Videos", vid_count, "#e74c3c");
        add_row("Audio", aud_count, "#9b59b6");
        add_row("Documents", doc_count, "#2ecc71");
        add_row("Archives", arch_count, "#f39c12");
        add_row("Other", other_count, "#95a5a6");
        
        layout->addWidget(breakdown);
        
        // Folder section
        if (folder_count > 0) {
            auto* folder_widget = new QWidget();
            folder_widget->setStyleSheet("background-color: #2c3e50; border-radius: 6px; padding: 6px;");
            auto* folder_layout = new QVBoxLayout(folder_widget);
            folder_layout->setContentsMargins(8, 4, 8, 4);
            folder_layout->setSpacing(2);
            
            auto* folder_header = new QLabel(QString(
                "<span style='color: #ecf0f1; font-size: 13px; font-weight: bold;'>Subfolders (%1)</span>"
            ).arg(folder_count));
            folder_layout->addWidget(folder_header);
            
            // Show up to 10 subfolders, with "and N more..." if needed
            int show_count = qMin(folder_count, 10);
            QString folder_list;
            for (int i = 0; i < show_count; ++i) {
                folder_list += QString("<span style='color: #bdc3c7; font-size: 11px;'>  %1</span><br>").arg(subfolders[i]);
            }
            if (folder_count > 10) {
                folder_list += QString("<span style='color: #95a5a6; font-size: 11px;'>  ...and %1 more</span>").arg(folder_count - 10);
            }
            auto* folder_list_label = new QLabel(folder_list);
            folder_list_label->setWordWrap(true);
            folder_layout->addWidget(folder_list_label);
            
            layout->addWidget(folder_widget);
        }
        
        layout->addStretch();
        
        auto* btn_layout = new QHBoxLayout();
        auto* cancel_btn = new QPushButton("Cancel");
        connect(cancel_btn, &QPushButton::clicked, &dashboard, &QDialog::reject);
        btn_layout->addWidget(cancel_btn);
        
        btn_layout->addStretch();
        
        auto* start_btn = new QPushButton("Start Sorting");
        start_btn->setStyleSheet(
            "QPushButton { padding: 10px 25px; background-color: #27ae60; "
            "color: white; font-weight: bold; border-radius: 6px; }"
            "QPushButton:hover { background-color: #2ecc71; }"
        );
        connect(start_btn, &QPushButton::clicked, &dashboard, &QDialog::accept);
        btn_layout->addWidget(start_btn);
        
        layout->addLayout(btn_layout);
        
        return dashboard.exec() == QDialog::Accepted;
    }
    
    bool validate_folder() {
        if (chosen_path_.isEmpty()) {
            QMessageBox::warning(this, "No Folder", "Please select a folder first.");
            return false;
        }
        
        // Ensure source_folders_ has at least the primary folder
        if (source_folders_.isEmpty()) {
            source_folders_ = {chosen_path_};
        }
        
        // Check that at least one folder has files
        bool has_files = false;
        for (const QString& src : source_folders_) {
            QDir folder(src);
            if (!folder.entryList(QDir::Files).isEmpty()) {
                has_files = true;
                break;
            }
        }
        if (!has_files) {
            QMessageBox::information(this, "Empty Folder", "The selected folders have no files to sort.");
            return false;
        }
        
        // Persist source folders for session resume
        db_manager_.save_source_folders(chosen_path_, source_folders_);
        
        return true;
    }
    
    void destroy_all_mode_widgets() {
        if (basic_widget_) {
            stack_->removeWidget(basic_widget_);
            basic_widget_->deleteLater();
            basic_widget_ = nullptr;
        }
        if (advanced_widget_) {
            stack_->removeWidget(advanced_widget_);
            advanced_widget_->deleteLater();
            advanced_widget_ = nullptr;
        }
        if (ai_widget_) {
            stack_->removeWidget(ai_widget_);
            ai_widget_->deleteLater();
            ai_widget_ = nullptr;
        }
    }
    
    void return_to_launcher() {
        stack_->setCurrentWidget(launcher_page_);
        setWindowTitle("File Tinder");
        check_session_state();
        // Restore the launcher page size so the window doesn't stay enlarged
        if (launcher_size_.isValid()) {
            resize(launcher_size_);
        }
        ensure_on_screen();
    }
    
    void check_session_state() {
        if (chosen_path_.isEmpty() || !resume_label_) return;
        int progress = db_manager_.get_session_progress_count(chosen_path_);
        if (progress > 0) {
            resume_label_->setText(QString("Session in progress: %1 files sorted. Click a mode to resume.")
                .arg(progress));
            resume_label_->setVisible(true);
        } else {
            resume_label_->setVisible(false);
        }
    }
    
    void launch_basic() {
        if (!validate_folder()) return;
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting basic mode");
        
        // Save launcher size only when leaving the launcher page
        if (stack_->currentWidget() == launcher_page_)
            launcher_size_ = size();
        
        // Destroy old widget if source folder changed
        if (basic_widget_ && basic_widget_->source_folder() != chosen_path_) {
            stack_->removeWidget(basic_widget_);
            basic_widget_->deleteLater();
            basic_widget_ = nullptr;
        }
        
        if (!basic_widget_) {
            basic_widget_ = new StandaloneFileTinderDialog(chosen_path_, db_manager_, this, source_folders_);
            connect(basic_widget_, &StandaloneFileTinderDialog::switch_to_advanced_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_advanced();
            });
            connect(basic_widget_, &StandaloneFileTinderDialog::switch_to_ai_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_ai();
            });
            connect(basic_widget_, &StandaloneFileTinderDialog::request_back, this, [this]() {
                return_to_launcher();
            });
            stack_->addWidget(basic_widget_);
            basic_widget_->initialize();
        }
        
        stack_->setCurrentWidget(basic_widget_);
        resize_for_mode("basic");
    }
    
    void launch_advanced() {
        if (!validate_folder()) return;
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting advanced mode");
        
        // Save launcher size before resizing for mode
        if (stack_->currentWidget() == launcher_page_)
            launcher_size_ = size();
        
        if (advanced_widget_ && advanced_widget_->source_folder() != chosen_path_) {
            stack_->removeWidget(advanced_widget_);
            advanced_widget_->deleteLater();
            advanced_widget_ = nullptr;
        }
        
        if (!advanced_widget_) {
            advanced_widget_ = new AdvancedFileTinderDialog(chosen_path_, db_manager_, this, source_folders_);
            connect(advanced_widget_, &AdvancedFileTinderDialog::switch_to_basic_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_basic();
            });
            connect(advanced_widget_, &StandaloneFileTinderDialog::switch_to_ai_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_ai();
            });
            connect(advanced_widget_, &StandaloneFileTinderDialog::request_back, this, [this]() {
                return_to_launcher();
            });
            stack_->addWidget(advanced_widget_);
            advanced_widget_->initialize();
        }
        
        stack_->setCurrentWidget(advanced_widget_);
        resize_for_mode("advanced");
    }
    
    void launch_ai() {
        if (!validate_folder()) return;
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting AI mode");
        
        // Save launcher size before resizing for mode
        if (stack_->currentWidget() == launcher_page_)
            launcher_size_ = size();
        
        if (ai_widget_ && ai_widget_->source_folder() != chosen_path_) {
            stack_->removeWidget(ai_widget_);
            ai_widget_->deleteLater();
            ai_widget_ = nullptr;
        }
        
        if (!ai_widget_) {
            ai_widget_ = new AiFileTinderDialog(chosen_path_, db_manager_, this, source_folders_);
            connect(ai_widget_, &AdvancedFileTinderDialog::switch_to_basic_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_basic();
            });
            connect(ai_widget_, &StandaloneFileTinderDialog::switch_to_advanced_mode, this, [this]() {
                skip_stats_on_next_launch_ = true;
                launch_advanced();
            });
            connect(ai_widget_, &StandaloneFileTinderDialog::request_back, this, [this]() {
                return_to_launcher();
            });
            stack_->addWidget(ai_widget_);
            ai_widget_->initialize();
        }
        
        stack_->setCurrentWidget(ai_widget_);
        resize_for_mode("ai");
    }
    
    void clear_session() {
        if (chosen_path_.isEmpty()) {
            QMessageBox::information(this, "No Folder", "Select a folder first to clear its session data.");
            return;
        }
        
        auto reply = QMessageBox::question(this, "Clear Session",
            QString("Clear all saved progress for:\n%1\n\nThis cannot be undone.").arg(chosen_path_),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            db_manager_.clear_session(chosen_path_);
            destroy_all_mode_widgets();
            if (resume_label_) resume_label_->setVisible(false);
            LOG_INFO("Launcher", QString("Session cleared for: %1").arg(chosen_path_));
            QMessageBox::information(this, "Session Cleared", "Saved progress has been cleared.");
        }
    }
    
    void show_undo_history() {
        if (chosen_path_.isEmpty()) {
            QMessageBox::information(this, "No Folder", "Select a folder first to view its undo history.");
            return;
        }
        
        auto log_entries = db_manager_.get_execution_log(chosen_path_);
        
        if (log_entries.empty()) {
            QMessageBox::information(this, "No History", "No executed actions to undo for this folder.");
            return;
        }
        
        QDialog dialog(this);
        dialog.setWindowTitle("Undo History");
        dialog.resize(ui::scaling::scaled(600), ui::scaling::scaled(400));
        
        auto* layout = new QVBoxLayout(&dialog);
        
        auto* info_label = new QLabel(QString("Executed actions for: %1\nClick Undo to reverse an action.")
            .arg(chosen_path_));
        info_label->setWordWrap(true);
        layout->addWidget(info_label);
        
        auto* filter_layout = new QHBoxLayout();
        filter_layout->addWidget(new QLabel("Filter:"));
        auto* history_filter = new QComboBox();
        history_filter->addItems({"All", "Moved", "Deleted"});
        filter_layout->addWidget(history_filter);
        filter_layout->addStretch();
        layout->addLayout(filter_layout);
        
        auto* table = new QTableWidget();
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({"Action", "File", "Destination", "Time", "Undo"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSortingEnabled(true);
        table->setRowCount(static_cast<int>(log_entries.size()));
        
        for (int i = 0; i < static_cast<int>(log_entries.size()); ++i) {
            const auto& [id, action, src, dst, ts] = log_entries[i];
            
            auto* action_item = new QTableWidgetItem(action);
            action_item->setFlags(action_item->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, action_item);
            
            auto* file_item = new QTableWidgetItem(src);
            file_item->setFlags(file_item->flags() & ~Qt::ItemIsEditable);
            file_item->setToolTip(src);
            table->setItem(i, 1, file_item);
            
            QString dest_display;
            if (action == "delete") {
                dest_display = dst.isEmpty() ? "(permanent)" : "(trash)";
            } else {
                dest_display = QFileInfo(dst).fileName();
            }
            auto* dest_item = new QTableWidgetItem(dest_display);
            dest_item->setFlags(dest_item->flags() & ~Qt::ItemIsEditable);
            dest_item->setToolTip(dst);
            table->setItem(i, 2, dest_item);
            
            auto* time_item = new QTableWidgetItem(ts);
            time_item->setFlags(time_item->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 3, time_item);
            
            auto* undo_btn = new QPushButton("Undo");
            undo_btn->setStyleSheet(
                "QPushButton { background-color: #e67e22; color: white; padding: 2px 8px; border-radius: 3px; }"
                "QPushButton:hover { background-color: #d35400; }"
                "QPushButton:disabled { background-color: #7f8c8d; color: #bdc3c7; }"
            );
            // Disable undo for permanently deleted files (no trash path)
            if (action == "delete" && dst.isEmpty()) {
                undo_btn->setEnabled(false);
                undo_btn->setText("Permanent");
                undo_btn->setToolTip("File was permanently deleted — cannot undo");
            }
            connect(undo_btn, &QPushButton::clicked, this, [this, id, action, src, dst, undo_btn, action_item]() {
                ExecutionLogEntry entry;
                entry.action = action;
                entry.source_path = src;
                entry.dest_path = dst;
                entry.success = true;
                
                if (FileTinderExecutor::undo_action(entry)) {
                    undo_btn->setEnabled(false);
                    undo_btn->setText("Done");
                    action_item->setText(action + " (undone)");
                    db_manager_.remove_execution_log_entry(id);
                    LOG_INFO("UndoHistory", QString("Undone: %1 %2").arg(action, src));
                } else {
                    QMessageBox::warning(this, "Undo Failed",
                        "Could not undo this action.\nThe file may have been modified or removed.");
                }
            });
            table->setCellWidget(i, 4, undo_btn);
        }
        
        table->resizeColumnsToContents();
        layout->addWidget(table, 1);
        
        connect(history_filter, &QComboBox::currentTextChanged, this, [table](const QString& filter) {
            for (int r = 0; r < table->rowCount(); ++r) {
                if (filter == "All") {
                    table->setRowHidden(r, false);
                } else {
                    auto* item = table->item(r, 0);
                    if (!item) continue;
                    QString action = item->text().toLower();
                    if (filter == "Moved") {
                        table->setRowHidden(r, !action.startsWith("move"));
                    } else if (filter == "Deleted") {
                        table->setRowHidden(r, !action.startsWith("delete"));
                    }
                }
            }
        });
        
        auto* btn_layout = new QHBoxLayout();
        btn_layout->addStretch();
        
        auto* clear_log_btn = new QPushButton("Clear History");
        clear_log_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #e74c3c; color: white; border-radius: 3px; }"
            "QPushButton:hover { background-color: #c0392b; }"
        );
        connect(clear_log_btn, &QPushButton::clicked, this, [this, &dialog]() {
            auto reply = QMessageBox::question(this, "Clear History",
                "This will remove all undo history. You won't be able to reverse past actions.\n\nProceed?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                db_manager_.clear_execution_log(chosen_path_);
                dialog.accept();
            }
        });
        btn_layout->addWidget(clear_log_btn);
        
        auto* close_btn = new QPushButton("Close");
        connect(close_btn, &QPushButton::clicked, &dialog, &QDialog::accept);
        btn_layout->addWidget(close_btn);
        
        layout->addLayout(btn_layout);
        
        dialog.exec();
    }
};

int main(int argc, char* argv[]) {
    QApplication qt_app(argc, argv);
    
    qt_app.setApplicationName("File Tinder");
    qt_app.setApplicationVersion("1.0.0");
    qt_app.setOrganizationName("FileTinderApp");
    
    // Initialize logging
    AppLogger::instance().set_minimum_severity(LogSeverity::Debug);
    LOG_INFO("Main", "File Tinder application started");
    
    // Use Fusion style for consistent look
    qt_app.setStyle(QStyleFactory::create("Fusion"));
    
    // Custom color scheme
    QPalette app_colors;
    app_colors.setColor(QPalette::Window, QColor(45, 45, 45));
    app_colors.setColor(QPalette::WindowText, QColor(230, 230, 230));
    app_colors.setColor(QPalette::Base, QColor(35, 35, 35));
    app_colors.setColor(QPalette::AlternateBase, QColor(55, 55, 55));
    app_colors.setColor(QPalette::Text, QColor(230, 230, 230));
    app_colors.setColor(QPalette::Button, QColor(50, 50, 50));
    app_colors.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    app_colors.setColor(QPalette::Highlight, QColor(0, 120, 212));
    app_colors.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    qt_app.setPalette(app_colors);
    
    FileTinderLauncher launcher_window;
    launcher_window.show();
    
    int exit_code = qt_app.exec();
    
    LOG_INFO("Main", "Application exiting");
    return exit_code;
}

#include "main.moc"
