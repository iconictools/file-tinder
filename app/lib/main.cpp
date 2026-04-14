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
#include <QMap>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QUrl>
#include <QDesktopServices>
#include <QFile>
#include <optional>

#include "DatabaseManager.hpp"
#include "StandaloneFileTinderDialog.hpp"
#include "AdvancedFileTinderDialog.hpp"
#include "AiFileTinderDialog.hpp"
#include "FileTinderExecutor.hpp"
#include "AppLogger.hpp"
#include "DiagnosticTool.hpp"
#include "ui_constants.hpp"

enum class AppModule {
    Tinder,
    Filer,
    AiFiler
};

struct ModuleDescriptor {
    AppModule module;
    QString id;
    QString app_name;
    QString button_title;
    QString description;
    QStringList standalone_executables;
};

class FileTinderLauncher : public QDialog {
    Q_OBJECT
    
public:
    FileTinderLauncher(const QString& initial_folder = QString(),
                      std::optional<AppModule> requested_module = std::nullopt,
                      bool request_suite = false,
                      QWidget* parent_widget = nullptr)
        : QDialog(parent_widget)
        , db_manager_()
        , chosen_path_()
        , requested_module_(requested_module)
        , request_suite_launch_(request_suite) {
        
        setWindowTitle("File Tinder Launcher");
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
        
        refresh_module_discovery();
        build_interface();
        
        // Load theme preference
        QSettings theme_settings("FileTinder", "FileTinder");
        is_dark_theme_ = theme_settings.value("darkTheme", true).toBool();
        apply_theme();
        
        // Pre-fill folder
        QSettings settings("FileTinder", "FileTinder");
        QString prefilled_folder = initial_folder;
        if (prefilled_folder.isEmpty()) {
            prefilled_folder = settings.value("lastFolder").toString();
        }
        if (!prefilled_folder.isEmpty() && QDir(prefilled_folder).exists()) {
            chosen_path_ = prefilled_folder;
            path_indicator_->setText(prefilled_folder);
            path_indicator_->setStyleSheet(
                "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
            );
            
            // Check for resumable session
            int progress = db_manager_.get_session_progress_count(prefilled_folder);
            if (progress > 0 && resume_label_) {
                resume_label_->setText(QString("Session in progress: %1 files sorted. Click a mode to resume.")
                    .arg(progress));
                resume_label_->setVisible(true);
            }
        }

        QTimer::singleShot(0, this, [this]() { handle_initial_module_request(); });
    }
    
private:
    DatabaseManager db_manager_;
    QString chosen_path_;
    QLabel* path_indicator_;
    QListWidget* recent_list_ = nullptr;
    QLabel* resume_label_ = nullptr;
    QLabel* modules_status_label_ = nullptr;
    bool skip_stats_on_next_launch_ = false;  // Skip stats dashboard on mode switch
    bool is_dark_theme_ = true;
    std::optional<AppModule> requested_module_;
    bool request_suite_launch_ = false;
    QMap<AppModule, QString> standalone_module_paths_;

    std::vector<ModuleDescriptor> module_catalog() const {
        return {
            {AppModule::Tinder, "tinder", "Iconic File Tinder",
             "File Tinder\n(Swipe sorting)", "Core swipe-style file triage",
             {"IconicFileTinder", "FileTinder", "iconic-file-tinder"}},
            {AppModule::Filer, "filer", "Iconic File Filer",
             "File Filer\n(Folder tree)", "Folder-mind-map filing workflow",
             {"IconicFileFiler", "FileFiler", "iconic-file-filer"}},
            {AppModule::AiFiler, "ai-filer", "Iconic File AI Filer",
             "File AI Filer\n(AI-assisted)", "AI-assisted filing and category suggestions",
             {"IconicFileAiFiler", "FileAiFiler", "iconic-file-ai-filer"}}
        };
    }

    QString module_display_name(AppModule module) const {
        for (const auto& descriptor : module_catalog()) {
            if (descriptor.module == module) return descriptor.app_name;
        }
        return "Unknown Module";
    }

    QString module_id(AppModule module) const {
        for (const auto& descriptor : module_catalog()) {
            if (descriptor.module == module) return descriptor.id;
        }
        return "unknown";
    }

    bool is_integrated_module_available(AppModule) const {
        return true;  // current build ships all modules in a single binary
    }

    bool is_module_available(AppModule module) const {
        return is_integrated_module_available(module) || standalone_module_paths_.contains(module);
    }

    int available_module_count() const {
        int available = 0;
        for (const auto& descriptor : module_catalog()) {
            if (is_module_available(descriptor.module)) available++;
        }
        return available;
    }

    void refresh_module_discovery() {
        standalone_module_paths_.clear();
        const QString app_dir = QCoreApplication::applicationDirPath();

        for (const auto& descriptor : module_catalog()) {
            for (const QString& executable : descriptor.standalone_executables) {
                const QString local_path = QDir(app_dir).absoluteFilePath(executable);
                if (QFile::exists(local_path)) {
                    standalone_module_paths_[descriptor.module] = local_path;
                    break;
                }
                const QString in_path = QStandardPaths::findExecutable(executable);
                if (!in_path.isEmpty()) {
                    standalone_module_paths_[descriptor.module] = in_path;
                    break;
                }
            }
        }
        update_modules_status_label();
    }

    QString module_install_status(AppModule module) const {
        if (standalone_module_paths_.contains(module)) {
            return "Integrated + Standalone detected";
        }
        if (is_integrated_module_available(module)) {
            return "Integrated";
        }
        return "Missing";
    }

    void update_modules_status_label() {
        if (!modules_status_label_) return;
        int standalone_count = standalone_module_paths_.size();
        modules_status_label_->setText(
            QString("Module suite ready: 3 integrated modules • %1 standalone companion(s) detected")
                .arg(standalone_count)
        );
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
            p.setColor(QPalette::Window, QColor(240, 240, 240));
            p.setColor(QPalette::WindowText, QColor(30, 30, 30));
            p.setColor(QPalette::Base, QColor(255, 255, 255));
            p.setColor(QPalette::AlternateBase, QColor(235, 235, 235));
            p.setColor(QPalette::Text, QColor(30, 30, 30));
            p.setColor(QPalette::Button, QColor(225, 225, 225));
            p.setColor(QPalette::ButtonText, QColor(30, 30, 30));
            p.setColor(QPalette::Highlight, QColor(0, 120, 212));
            p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        }
        QApplication::setPalette(p);
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
        return QDialog::eventFilter(obj, event);
    }
    
    void build_interface() {
        auto* root_layout = new QVBoxLayout(this);
        root_layout->setContentsMargins(25, 25, 25, 25);
        root_layout->setSpacing(18);
        
        // App header
        auto* app_title = new QLabel("ICONIC FILE SUITE");
        app_title->setStyleSheet("font-size: 28px; font-weight: bold; color: #0078d4;");
        app_title->setAlignment(Qt::AlignCenter);
        root_layout->addWidget(app_title);
        
        auto* app_desc = new QLabel("Modular launcher for File Tinder, File Filer, and File AI Filer");
        app_desc->setStyleSheet("font-size: 13px; color: #888888;");
        app_desc->setAlignment(Qt::AlignCenter);
        root_layout->addWidget(app_desc);
        
        root_layout->addSpacing(15);
        
        // Folder picker section
        auto* picker_label = new QLabel("Choose folder to organize:");
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
        
        // Recent folders list (middle-click to remove)
        QStringList recent = db_manager_.get_recent_folders(5);
        if (!recent.isEmpty()) {
            auto* recent_label = new QLabel("Recent folders (click to select, middle-click to remove):");
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
                    chosen_path_ = path;
                    path_indicator_->setText(path);
                    path_indicator_->setStyleSheet(
                        "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
                    );
                } else {
                    QMessageBox::warning(this, "Folder Not Found",
                        QString("The folder no longer exists:\n%1").arg(path));
                }
                recent_list->clearSelection();
            });
            
            // Middle-click to remove
            recent_list->viewport()->installEventFilter(this);
            recent_list_ = recent_list;
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
        
        // Module launch buttons
        auto* modes_label = new QLabel("Choose module:");
        modes_label->setStyleSheet("font-weight: bold; font-size: 12px;");
        root_layout->addWidget(modes_label);
        
        auto* modes_row = new QHBoxLayout();
        modes_row->setSpacing(12);
        
        auto* basic_mode_btn = new QPushButton("File Tinder\n(Swipe sorting)");
        basic_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        basic_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #107c10; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #0e6b0e; }"
        );
        connect(basic_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_basic);
        modes_row->addWidget(basic_mode_btn);
        
        auto* adv_mode_btn = new QPushButton("File Filer\n(Folder tree filing)");
        adv_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        adv_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #5c2d91; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #4a2473; }"
        );
        connect(adv_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_advanced);
        modes_row->addWidget(adv_mode_btn);
        
        auto* ai_mode_btn = new QPushButton("File AI Filer\n(AI-assisted filing)");
        ai_mode_btn->setMinimumSize(ui::scaling::scaled(180), ui::scaling::scaled(70));
        ai_mode_btn->setStyleSheet(
            "QPushButton { padding: 12px; background-color: #2980b9; color: white; border: none; font-size: 13px; }"
            "QPushButton:hover { background-color: #1a6fa0; }"
        );
        connect(ai_mode_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_ai);
        modes_row->addWidget(ai_mode_btn);
        
        root_layout->addLayout(modes_row);

        auto* suite_row = new QHBoxLayout();

        auto* launch_suite_btn = new QPushButton("Launch Suite...");
        launch_suite_btn->setStyleSheet(
            "QPushButton { padding: 8px 14px; background-color: #0b5fa5; color: white; border: none; font-size: 12px; }"
            "QPushButton:hover { background-color: #0a4f8a; }"
        );
        connect(launch_suite_btn, &QPushButton::clicked, this, &FileTinderLauncher::launch_suite_selector);
        suite_row->addWidget(launch_suite_btn);

        auto* modules_btn = new QPushButton("Manage Modules...");
        modules_btn->setStyleSheet(
            "QPushButton { padding: 8px 14px; background-color: #4a4a4a; color: #e0e0e0; border: 1px solid #555555; font-size: 12px; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(modules_btn, &QPushButton::clicked, this, &FileTinderLauncher::open_module_manager);
        suite_row->addWidget(modules_btn);
        suite_row->addStretch();

        root_layout->addLayout(suite_row);

        modules_status_label_ = new QLabel();
        modules_status_label_->setStyleSheet(
            "padding: 6px 10px; background-color: #1d2b3a; border: 1px solid #35526f; "
            "color: #8ec7ff; font-size: 10px; border-radius: 4px;");
        root_layout->addWidget(modules_status_label_);
        update_modules_status_label();
        
        // Tools row: Clear Session + Undo History + Diagnostics
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
        
        auto* diag_btn = new QPushButton("Diagnostics");
        diag_btn->setStyleSheet(
            "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
            "QPushButton:hover { background-color: #555555; }"
        );
        connect(diag_btn, &QPushButton::clicked, this, &FileTinderLauncher::open_diagnostics);
        tools_row->addWidget(diag_btn);
        
        root_layout->addLayout(tools_row);
        
        // Hotkey hint
        auto* hint_text = new QLabel("Keys: Left=Delete | Down=Skip | Z=Undo | F=File List | Basic: Right=Keep | Advanced/AI: K=Keep, 1-0=Quick Access, Tab=Grid Nav");
        hint_text->setStyleSheet("color: #666666; font-size: 10px; padding-top: 8px;");
        hint_text->setAlignment(Qt::AlignCenter);
        hint_text->setWordWrap(true);
        root_layout->addWidget(hint_text);
    }
    
    void pick_folder() {
        QString path = QFileDialog::getExistingDirectory(this, "Pick Folder", QDir::homePath());
        if (!path.isEmpty()) {
            chosen_path_ = path;
            path_indicator_->setText(path);
            path_indicator_->setStyleSheet(
                "padding: 8px 12px; background-color: #1a3a1a; border: 1px solid #2a5a2a; color: #88cc88;"
            );
            LOG_INFO("Launcher", QString("Folder selected: %1").arg(path));
        }
    }
    
    bool show_pre_session_stats() {
        QDir dir(chosen_path_);
        QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        
        if (files.isEmpty()) {
            QMessageBox::information(this, "Empty Folder", "This folder has no files to sort.");
            return false;
        }
        
        // Collect file stats (show progress for large dirs)
        qint64 total_size = 0;
        int img_count = 0, vid_count = 0, aud_count = 0, doc_count = 0, arch_count = 0, other_count = 0;
        QMimeDatabase mime_db;
        
        QProgressDialog* progress = nullptr;
        if (files.size() > 200) {
            progress = new QProgressDialog("Analyzing files...", QString(), 0, files.size(), this);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->show();
        }
        
        for (int i = 0; i < files.size(); ++i) {
            const QString& file = files[i];
            QFileInfo info(dir.absoluteFilePath(file));
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
        
        // Collect subfolder info
        QStringList subfolders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
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
        dashboard.setMinimumSize(ui::scaling::scaled(450), ui::scaling::scaled(350));
        
        auto* layout = new QVBoxLayout(&dashboard);
        
        auto* header = new QLabel(chosen_path_);
        header->setStyleSheet("font-size: 13px; font-weight: bold; color: #3498db;");
        header->setWordWrap(true);
        layout->addWidget(header);
        
        auto* summary = new QLabel(QString(
            "<div style='font-size: 14px; margin: 10px 0;'>"
            "<b>%1 files</b> &middot; %2 total &middot; <b>%3 subfolders</b>"
            "</div>"
        ).arg(files.size()).arg(size_str).arg(folder_count));
        layout->addWidget(summary);
        
        // Type breakdown
        auto* breakdown = new QWidget();
        breakdown->setStyleSheet("background-color: #34495e; border-radius: 8px; padding: 12px;");
        auto* bd_layout = new QVBoxLayout(breakdown);
        
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
            folder_widget->setStyleSheet("background-color: #2c3e50; border-radius: 8px; padding: 10px;");
            auto* folder_layout = new QVBoxLayout(folder_widget);
            
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
        
        QDir folder(chosen_path_);
        if (folder.entryList(QDir::Files).isEmpty()) {
            QMessageBox::information(this, "Empty Folder", "This folder has no files to sort.");
            return false;
        }
        
        return true;
    }
    
    void launch_basic() {
        if (!validate_folder()) return;
        // Skip stats dashboard on mode switch (already shown once)
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting basic mode");
        
        auto* dlg = new StandaloneFileTinderDialog(chosen_path_, db_manager_, this);
        
        connect(dlg, &StandaloneFileTinderDialog::switch_to_advanced_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            // Defer mode switch to break recursive exec() chain
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_advanced);
        });
        
        connect(dlg, &StandaloneFileTinderDialog::switch_to_ai_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_ai);
        });
        
        dlg->initialize();
        dlg->exec();
        dlg->deleteLater();
    }
    
    void launch_advanced() {
        if (!validate_folder()) return;
        // Skip stats dashboard on mode switch (already shown once)
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting advanced mode");
        
        auto* dlg = new AdvancedFileTinderDialog(chosen_path_, db_manager_, this);
        
        connect(dlg, &AdvancedFileTinderDialog::switch_to_basic_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            // Defer mode switch to break recursive exec() chain
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_basic);
        });
        
        connect(dlg, &StandaloneFileTinderDialog::switch_to_ai_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_ai);
        });
        
        dlg->initialize();
        dlg->exec();
        dlg->deleteLater();
    }
    
    void launch_ai() {
        if (!validate_folder()) return;
        if (skip_stats_on_next_launch_) {
            skip_stats_on_next_launch_ = false;
        } else {
            if (!show_pre_session_stats()) return;
        }
        
        LOG_INFO("Launcher", "Starting AI mode");
        
        auto* dlg = new AiFileTinderDialog(chosen_path_, db_manager_, this);
        
        connect(dlg, &AiFileTinderDialog::switch_to_basic_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_basic);
        });
        
        connect(dlg, &AiFileTinderDialog::switch_to_advanced_mode, this, [this, dlg]() {
            dlg->done(QDialog::Accepted);
            skip_stats_on_next_launch_ = true;
            QTimer::singleShot(0, this, &FileTinderLauncher::launch_advanced);
        });
        
        dlg->initialize();
        dlg->exec();
        dlg->deleteLater();
    }

    bool launch_module(AppModule module) {
        if (!is_module_available(module)) {
            QMessageBox::warning(this, "Module Missing",
                QString("%1 is not available in this installation.").arg(module_display_name(module)));
            return false;
        }

        switch (module) {
        case AppModule::Tinder:
            launch_basic();
            return true;
        case AppModule::Filer:
            launch_advanced();
            return true;
        case AppModule::AiFiler:
            launch_ai();
            return true;
        }
        return false;
    }

    bool launch_standalone_module(AppModule module) {
        if (!standalone_module_paths_.contains(module)) return false;

        QStringList args;
        args << QString("--module=%1").arg(module_id(module));
        if (!chosen_path_.isEmpty()) {
            args << QString("--folder=%1").arg(chosen_path_);
        }

        const bool started = QProcess::startDetached(standalone_module_paths_[module], args);
        if (!started) {
            QMessageBox::warning(this, "Launch Failed",
                QString("Could not launch standalone app for %1.").arg(module_display_name(module)));
        }
        return started;
    }

    void open_module_manager() {
        refresh_module_discovery();

        QDialog manager(this);
        manager.setWindowTitle("Module Manager");
        manager.setMinimumSize(ui::scaling::scaled(520), ui::scaling::scaled(300));

        auto* layout = new QVBoxLayout(&manager);
        layout->addWidget(new QLabel("Installed modules and companion standalone apps:"));

        auto* table = new QTableWidget();
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Module", "Status", "Standalone Path"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        const auto modules = module_catalog();
        table->setRowCount(static_cast<int>(modules.size()));
        for (int row = 0; row < static_cast<int>(modules.size()); ++row) {
            const auto& descriptor = modules[row];
            table->setItem(row, 0, new QTableWidgetItem(descriptor.app_name));
            table->setItem(row, 1, new QTableWidgetItem(module_install_status(descriptor.module)));
            table->setItem(row, 2, new QTableWidgetItem(
                standalone_module_paths_.value(descriptor.module, "(none detected)")));
        }
        table->resizeColumnsToContents();
        layout->addWidget(table, 1);

        auto* note = new QLabel(
            "Missing modules can be installed from release packages and are auto-detected "
            "when found in PATH or beside this executable.");
        note->setWordWrap(true);
        note->setStyleSheet("color: #9fb7cf; font-size: 10px;");
        layout->addWidget(note);

        auto* btn_row = new QHBoxLayout();
        auto* refresh_btn = new QPushButton("Rescan");
        connect(refresh_btn, &QPushButton::clicked, this, [this, table]() {
            refresh_module_discovery();
            const auto modules = module_catalog();
            for (int row = 0; row < static_cast<int>(modules.size()); ++row) {
                const auto& descriptor = modules[row];
                table->item(row, 1)->setText(module_install_status(descriptor.module));
                table->item(row, 2)->setText(
                    standalone_module_paths_.value(descriptor.module, "(none detected)"));
            }
        });
        btn_row->addWidget(refresh_btn);

        auto* download_btn = new QPushButton("Download Modules");
        connect(download_btn, &QPushButton::clicked, &manager, [this]() {
            const QUrl releases_url("https://github.com/iconictools/file-tinder/releases");
            QDesktopServices::openUrl(releases_url);
            QMessageBox::information(this, "Download",
                "Release page opened. Download Iconic File Filer and Iconic File AI Filer packages, "
                "then relaunch or click Rescan.");
        });
        btn_row->addWidget(download_btn);

        btn_row->addStretch();
        auto* close_btn = new QPushButton("Close");
        connect(close_btn, &QPushButton::clicked, &manager, &QDialog::accept);
        btn_row->addWidget(close_btn);
        layout->addLayout(btn_row);

        manager.exec();
    }

    void launch_suite_selector() {
        refresh_module_discovery();

        QDialog selector(this);
        selector.setWindowTitle("Launch Module Suite");
        selector.setMinimumSize(ui::scaling::scaled(430), ui::scaling::scaled(260));

        auto* layout = new QVBoxLayout(&selector);
        auto* help = new QLabel("Choose which modules to open for this session:");
        help->setWordWrap(true);
        layout->addWidget(help);

        QMap<AppModule, QCheckBox*> checkboxes;
        for (const auto& descriptor : module_catalog()) {
            auto* checkbox = new QCheckBox(
                QString("%1 — %2").arg(descriptor.app_name, descriptor.description));
            checkbox->setChecked(is_module_available(descriptor.module));
            checkbox->setEnabled(is_module_available(descriptor.module));
            if (!is_module_available(descriptor.module)) {
                checkbox->setToolTip("Not installed");
            }
            layout->addWidget(checkbox);
            checkboxes[descriptor.module] = checkbox;
        }

        auto* standalone_opt = new QCheckBox(
            "Open standalone companions when available (instead of integrated dialogs)");
        standalone_opt->setChecked(false);
        layout->addWidget(standalone_opt);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &selector, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &selector, &QDialog::reject);
        layout->addWidget(buttons);

        if (selector.exec() != QDialog::Accepted) return;

        std::vector<AppModule> selected;
        for (const auto& descriptor : module_catalog()) {
            auto* checkbox = checkboxes.value(descriptor.module, nullptr);
            if (checkbox && checkbox->isChecked()) selected.push_back(descriptor.module);
        }

        if (selected.empty()) {
            QMessageBox::information(this, "No Modules Selected", "Select at least one module.");
            return;
        }

        bool first = true;
        const bool use_standalone = standalone_opt->isChecked();
        for (AppModule module : selected) {
            if (!first) skip_stats_on_next_launch_ = true;
            if (use_standalone && launch_standalone_module(module)) {
                // launched detached companion
            } else {
                launch_module(module);
            }
            first = false;
        }
    }

    void handle_initial_module_request() {
        if (!requested_module_ && !request_suite_launch_) return;

        refresh_module_discovery();
        if (request_suite_launch_) {
            launch_suite_selector();
            return;
        }

        if (!requested_module_) return;
        const AppModule module = *requested_module_;
        if (!is_module_available(module)) return;

        if (available_module_count() > 1) {
            QMessageBox prompt(this);
            prompt.setIcon(QMessageBox::Question);
            prompt.setWindowTitle("Open Module or Suite");
            prompt.setText(QString("%1 was opened as a standalone module.")
                .arg(module_display_name(module)));
            prompt.setInformativeText("Do you want to attach to a multi-module suite session?");
            auto* module_btn = prompt.addButton("Module Only", QMessageBox::AcceptRole);
            auto* suite_btn = prompt.addButton("Open Suite Selector", QMessageBox::ActionRole);
            prompt.addButton(QMessageBox::Cancel);
            prompt.exec();
            if (prompt.clickedButton() == suite_btn) {
                launch_suite_selector();
                return;
            }
            if (prompt.clickedButton() != module_btn) return;
        }

        launch_module(module);
    }
    
    void open_diagnostics() {
        LOG_INFO("Launcher", "Opening diagnostic tool");
        DiagnosticTool diag(db_manager_, this);
        diag.exec();
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
        dialog.setMinimumSize(600, 400);
        
        auto* layout = new QVBoxLayout(&dialog);
        
        auto* info_label = new QLabel(QString("Executed actions for: %1\nClick Undo to reverse an action.")
            .arg(chosen_path_));
        info_label->setWordWrap(true);
        layout->addWidget(info_label);
        
        auto* table = new QTableWidget();
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({"Action", "File", "Destination", "Time", "Undo"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setRowCount(static_cast<int>(log_entries.size()));
        
        for (int i = 0; i < static_cast<int>(log_entries.size()); ++i) {
            const auto& [id, action, src, dst, ts] = log_entries[i];
            
            auto* action_item = new QTableWidgetItem(action);
            action_item->setFlags(action_item->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, action_item);
            
            auto* file_item = new QTableWidgetItem(QFileInfo(src).fileName());
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

    QString initial_folder;
    std::optional<AppModule> requested_module = std::nullopt;
    bool request_suite = false;

    const QStringList args = QCoreApplication::arguments();
    for (const QString& arg : args) {
        if (arg == "--suite") {
            request_suite = true;
            continue;
        }
        if (arg.startsWith("--folder=")) {
            initial_folder = arg.mid(QString("--folder=").size());
            continue;
        }
        if (arg.startsWith("--module=")) {
            const QString value = arg.mid(QString("--module=").size()).trimmed().toLower();
            if (value == "tinder" || value == "basic") {
                requested_module = AppModule::Tinder;
            } else if (value == "filer" || value == "advanced") {
                requested_module = AppModule::Filer;
            } else if (value == "ai-filer" || value == "ai") {
                requested_module = AppModule::AiFiler;
            }
        }
    }

    FileTinderLauncher launcher_window(initial_folder, requested_module, request_suite);
    launcher_window.show();
    
    int exit_code = qt_app.exec();
    
    LOG_INFO("Main", "Application exiting");
    return exit_code;
}

#include "main.moc"
