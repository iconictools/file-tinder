#include "ManualEditDialog.hpp"
#include "ui_constants.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>

ManualEditDialog::ManualEditDialog(const QString& source_folder,
                                   const QStringList& initial_folders,
                                   QWidget* parent)
    : QDialog(parent)
    , source_folder_(source_folder)
{
    setup_ui();
    if (!initial_folders.isEmpty()) {
        populate_tree(initial_folders);
        sync_tree_to_text();
    }
}

void ManualEditDialog::setup_ui() {
    setWindowTitle("Manual Edit - Folder Configuration");
    setMinimumSize(ui::scaling::scaled(600), ui::scaling::scaled(480));

    auto* main_layout = new QVBoxLayout(this);

    // Title bar
    auto* title = new QLabel("Manual Edit - Folder Configuration");
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db; padding: 4px;");
    main_layout->addWidget(title);

    // View toggle
    auto* toggle_layout = new QHBoxLayout();
    view_toggle_ = new QCheckBox("Text View");
    view_toggle_->setToolTip("Toggle between List View and Text View");
    toggle_layout->addWidget(view_toggle_);
    toggle_layout->addStretch();
    main_layout->addLayout(toggle_layout);

    // Stacked widget for list/text views
    stack_ = new QStackedWidget();

    // Tree view (index 0)
    tree_ = new QTreeWidget();
    tree_->setHeaderLabels({"Folder Name", "Full Path", "Virtual"});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setEditTriggers(QAbstractItemView::DoubleClicked);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree_->setStyleSheet(
        "QTreeWidget { background: #1a1a2e; color: #e0e0e0; font-size: 11px; }"
        "QTreeWidget::item:selected { background: #3498db; }"
        "QHeaderView::section { background: #2d2d44; color: #bdc3c7; padding: 4px; }");
    tree_->installEventFilter(this);
    stack_->addWidget(tree_);

    // Text view (index 1)
    text_edit_ = new QTextEdit();
    text_edit_->setPlaceholderText("One folder per line. Use / for subfolders.");
    text_edit_->setStyleSheet(
        "QTextEdit { background: #1a1a2e; color: #e0e0e0; "
        "font-family: monospace; font-size: 11px; }");
    stack_->addWidget(text_edit_);

    stack_->setCurrentIndex(0);
    main_layout->addWidget(stack_, 1);

    // Note label
    auto* note = new QLabel("One folder per line. Use / for subfolders.");
    note->setStyleSheet("color: #95a5a6; font-size: 10px;");
    main_layout->addWidget(note);

    // Action buttons row
    auto* action_layout = new QHBoxLayout();
    add_btn_ = new QPushButton("Add Folder");
    add_sub_btn_ = new QPushButton("Add Subfolder");
    delete_btn_ = new QPushButton("Delete Selected");
    add_sub_btn_->setEnabled(false);
    delete_btn_->setEnabled(false);

    QString btn_style = "QPushButton { padding: 4px 12px; border-radius: 3px; }";
    add_btn_->setStyleSheet(btn_style);
    add_sub_btn_->setStyleSheet(btn_style);
    delete_btn_->setStyleSheet(btn_style + " QPushButton { color: #e74c3c; }");

    action_layout->addWidget(add_btn_);
    action_layout->addWidget(add_sub_btn_);
    action_layout->addWidget(delete_btn_);
    action_layout->addStretch();
    main_layout->addLayout(action_layout);

    // OK / Cancel
    auto* btn_layout = new QHBoxLayout();
    auto* cancel_btn = new QPushButton("Cancel");
    auto* ok_btn = new QPushButton("OK");
    ok_btn->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; "
        "padding: 6px 20px; border-radius: 4px; }");
    cancel_btn->setStyleSheet("QPushButton { padding: 6px 20px; border-radius: 4px; }");
    btn_layout->addStretch();
    btn_layout->addWidget(cancel_btn);
    btn_layout->addWidget(ok_btn);
    main_layout->addLayout(btn_layout);

    // Connections
    connect(view_toggle_, &QCheckBox::toggled, this, [this](bool text_mode) {
        if (text_mode) {
            sync_tree_to_text();
            stack_->setCurrentIndex(1);
        } else {
            sync_text_to_tree();
            stack_->setCurrentIndex(0);
        }
    });

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        bool has_sel = !tree_->selectedItems().isEmpty();
        add_sub_btn_->setEnabled(has_sel);
        delete_btn_->setEnabled(has_sel);
    });

    connect(add_btn_, &QPushButton::clicked, this, &ManualEditDialog::add_folder);
    connect(add_sub_btn_, &QPushButton::clicked, this, &ManualEditDialog::add_subfolder);
    connect(delete_btn_, &QPushButton::clicked, this, &ManualEditDialog::delete_selected);
    connect(ok_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
}

void ManualEditDialog::populate_tree(const QStringList& folders) {
    tree_->clear();

    // Build hierarchy: "Images" is top-level, "Images/Vacation" is child
    // Paths may be absolute (under source_folder_) or relative
    QMap<QString, QTreeWidgetItem*> item_map;

    QStringList relative_paths;
    for (const QString& folder : folders) {
        QString rel = folder;
        if (rel.startsWith(source_folder_)) {
            rel = QDir(source_folder_).relativeFilePath(rel);
        }
        if (rel == "." || rel.isEmpty()) continue;
        relative_paths.append(rel);
    }
    relative_paths.sort(Qt::CaseInsensitive);

    for (const QString& rel : relative_paths) {
        QStringList parts = rel.split('/', Qt::SkipEmptyParts);
        QString accumulated;
        QTreeWidgetItem* parent_item = nullptr;

        for (int i = 0; i < parts.size(); ++i) {
            if (!accumulated.isEmpty()) accumulated += '/';
            accumulated += parts[i];

            if (item_map.contains(accumulated)) {
                parent_item = item_map[accumulated];
                continue;
            }

            auto* item = new QTreeWidgetItem();
            item->setText(0, parts[i]);
            QString full_path = QDir::cleanPath(source_folder_ + '/' + accumulated);
            item->setText(1, full_path);
            item->setText(2, QDir(full_path).exists() ? "no" : "yes");
            item->setFlags(item->flags() | Qt::ItemIsEditable);

            if (parent_item) {
                parent_item->addChild(item);
            } else {
                tree_->addTopLevelItem(item);
            }
            item_map[accumulated] = item;
            parent_item = item;
        }
    }

    tree_->expandAll();
}

QStringList ManualEditDialog::tree_to_list() const {
    QStringList result;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        collect_paths(tree_->topLevelItem(i), QString(), result);
    }
    return result;
}

void ManualEditDialog::collect_paths(QTreeWidgetItem* item, const QString& prefix,
                                     QStringList& out) const {
    QString name = item->text(0).trimmed();
    if (name.isEmpty()) return;

    QString path = prefix.isEmpty() ? name : (prefix + '/' + name);
    out.append(path);

    for (int i = 0; i < item->childCount(); ++i) {
        collect_paths(item->child(i), path, out);
    }
}

void ManualEditDialog::sync_tree_to_text() {
    QStringList paths = tree_to_list();
    text_edit_->setPlainText(paths.join('\n'));
}

void ManualEditDialog::sync_text_to_tree() {
    QStringList lines = text_edit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    QStringList cleaned;
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) cleaned.append(trimmed);
    }
    populate_tree(cleaned);
}

void ManualEditDialog::add_folder() {
    bool ok;
    QString name = QInputDialog::getText(this, "Add Folder",
        "Folder name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    name = name.trimmed();
    auto* item = new QTreeWidgetItem();
    item->setText(0, name);
    QString full_path = QDir::cleanPath(source_folder_ + '/' + name);
    item->setText(1, full_path);
    item->setText(2, QDir(full_path).exists() ? "no" : "yes");
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    tree_->addTopLevelItem(item);
}

void ManualEditDialog::add_subfolder() {
    QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty()) return;

    bool ok;
    QString name = QInputDialog::getText(this, "Add Subfolder",
        "Subfolder name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    name = name.trimmed();
    QTreeWidgetItem* parent = sel.first();
    auto* item = new QTreeWidgetItem();
    item->setText(0, name);
    QString parent_path = parent->text(1);
    QString full_path = QDir::cleanPath(parent_path + '/' + name);
    item->setText(1, full_path);
    item->setText(2, QDir(full_path).exists() ? "no" : "yes");
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    parent->addChild(item);
    parent->setExpanded(true);
}

void ManualEditDialog::delete_selected() {
    QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty()) return;

    // Check if any selected items have children
    bool has_children = false;
    for (auto* item : sel) {
        if (item->childCount() > 0) {
            has_children = true;
            break;
        }
    }

    if (has_children) {
        auto reply = QMessageBox::question(this, "Delete Folders",
            "Some selected folders contain subfolders. Delete them all?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    for (auto* item : sel) {
        delete item;
    }
}

QStringList ManualEditDialog::get_folders() const {
    QStringList result;

    // Get from whichever view is active
    if (stack_->currentIndex() == 0) {
        // Tree view
        QStringList rel = tree_to_list();
        for (const QString& r : rel) {
            if (r.startsWith(source_folder_)) {
                result.append(r);
            } else {
                result.append(QDir::cleanPath(source_folder_ + '/' + r));
            }
        }
    } else {
        // Text view
        QStringList lines = text_edit_->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            if (trimmed.startsWith(source_folder_)) {
                result.append(trimmed);
            } else {
                result.append(QDir::cleanPath(source_folder_ + '/' + trimmed));
            }
        }
    }

    // Deduplicate
    QStringList unique;
    QSet<QString> seen;
    for (const QString& f : result) {
        if (!seen.contains(f)) {
            seen.insert(f);
            unique.append(f);
        }
    }
    return unique;
}

bool ManualEditDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == tree_) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::MiddleButton) {
                QTreeWidgetItem* item = tree_->itemAt(me->pos());
                if (item) {
                    bool has_children = item->childCount() > 0;
                    if (has_children) {
                        auto reply = QMessageBox::question(this, "Delete Folder",
                            QString("Delete '%1' and its subfolders?").arg(item->text(0)),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply != QMessageBox::Yes) return true;
                    }
                    delete item;
                    return true;
                }
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
                delete_selected();
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}
