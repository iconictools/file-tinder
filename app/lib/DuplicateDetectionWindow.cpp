#include "DuplicateDetectionWindow.hpp"
#include "StandaloneFileTinderDialog.hpp"
#include "ui_constants.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QApplication>

DuplicateDetectionWindow::DuplicateDetectionWindow(
    std::vector<FileToProcess>& files,
    const QString& source_folder,
    QWidget* parent)
    : QDialog(parent)
    , files_(files)
    , source_folder_(source_folder)
{
    setWindowTitle("Duplicate Detection");
    build_ui();
    detect_duplicates();
}

void DuplicateDetectionWindow::build_ui() {
    setMinimumSize(ui::scaling::scaled(600), ui::scaling::scaled(450));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* header = new QLabel("Duplicate files detected. Select files to delete.");
    header->setStyleSheet("font-size: 13px; font-weight: bold; color: #ecf0f1;");
    layout->addWidget(header);

    auto* hint = new QLabel("Files are grouped by name+size. Use 'Verify with Hash' for content-level matching.");
    hint->setStyleSheet("color: #888; font-size: 10px;");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // Tree view: groups as parents, files as children
    tree_ = new QTreeWidget();
    tree_->setHeaderLabels({"File", "Size", "Modified", "Path"});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setAlternatingRowColors(true);
    tree_->setStyleSheet(
        "QTreeWidget { background-color: #1e1e1e; border: 1px solid #404040; color: #ecf0f1; }"
        "QTreeWidget::item { padding: 2px; }"
        "QTreeWidget::item:selected { background-color: #0078d4; }"
        "QTreeWidget::item:alternate { background-color: #252525; }");
    tree_->header()->setStretchLastSection(true);
    tree_->setSortingEnabled(true);
    tree_->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(tree_, 1);

    status_label_ = new QLabel();
    status_label_->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(status_label_);

    // Buttons
    auto* btn_row = new QHBoxLayout();

    verify_btn_ = new QPushButton("Verify with Hash (SHA-256)");
    verify_btn_->setStyleSheet(
        "QPushButton { padding: 6px 14px; background-color: #2980b9; color: white; border: none; border-radius: 3px; }"
        "QPushButton:hover { background-color: #3498db; }");
    connect(verify_btn_, &QPushButton::clicked, this, &DuplicateDetectionWindow::on_verify_with_hash);
    btn_row->addWidget(verify_btn_);

    btn_row->addStretch();

    auto* keep_newest_btn = new QPushButton("Keep Newest");
    keep_newest_btn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    keep_newest_btn->setToolTip("Select all duplicates except the newest file in each group for deletion");
    connect(keep_newest_btn, &QPushButton::clicked, this, &DuplicateDetectionWindow::on_keep_newest);
    btn_row->addWidget(keep_newest_btn);

    delete_btn_ = new QPushButton("Delete Selected");
    delete_btn_->setStyleSheet(
        "QPushButton { padding: 6px 14px; background-color: #e74c3c; color: white; border: none; border-radius: 3px; }"
        "QPushButton:hover { background-color: #c0392b; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    delete_btn_->setEnabled(false);
    connect(delete_btn_, &QPushButton::clicked, this, &DuplicateDetectionWindow::on_delete_selected);
    btn_row->addWidget(delete_btn_);

    auto* close_btn = new QPushButton("Close");
    close_btn->setStyleSheet(
        "QPushButton { padding: 6px 14px; background-color: #4a4a4a; color: #ccc; border: 1px solid #555; border-radius: 3px; }"
        "QPushButton:hover { background-color: #555; }");
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    btn_row->addWidget(close_btn);

    layout->addLayout(btn_row);

    // Enable delete button when items are selected
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        int count = 0;
        qint64 total_selected_size = 0;
        for (auto* item : tree_->selectedItems()) {
            if (item->parent()) {
                ++count;
                int fi = item->data(0, Qt::UserRole).toInt();
                if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                    total_selected_size += files_[fi].size;
                }
            }
        }
        delete_btn_->setEnabled(count > 0);
        delete_btn_->setText(count > 0 ? QString("Delete Selected (%1)").arg(count) : "Delete Selected");
        if (count > 0) {
            delete_btn_->setStyleSheet(
                "QPushButton { background-color: #c0392b; color: white; font-weight: bold; padding: 6px 16px; border-radius: 4px; }"
                "QPushButton:hover { background-color: #e74c3c; }");
            QString size_str;
            if (total_selected_size < 1024LL) size_str = QString("%1 B").arg(total_selected_size);
            else if (total_selected_size < 1024LL*1024) size_str = QString("%1 KB").arg(total_selected_size/1024.0, 0, 'f', 1);
            else if (total_selected_size < 1024LL*1024*1024) size_str = QString("%1 MB").arg(total_selected_size/(1024.0*1024.0), 0, 'f', 1);
            else size_str = QString("%1 GB").arg(total_selected_size/(1024.0*1024.0*1024.0), 0, 'f', 2);
            status_label_->setText(QString("%1 file(s) selected — %2 recoverable space")
                                   .arg(count).arg(size_str));
        } else {
            delete_btn_->setStyleSheet(
                "QPushButton { background-color: #e74c3c; color: white; padding: 6px 16px; border-radius: 4px; }"
                "QPushButton:hover { background-color: #c0392b; }");
        }
    });
}

void DuplicateDetectionWindow::detect_duplicates() {
    // Phase 1: Group by name + size
    QMap<QString, QList<int>> name_size_groups;
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
        const auto& f = files_[i];
        if (f.is_directory) continue;
        QString key = QString("%1|%2").arg(f.name.toLower()).arg(f.size);
        name_size_groups[key].append(i);
    }

    // Keep only groups with 2+ files
    groups_.clear();
    tree_->clear();

    int total_dupes = 0;
    for (auto it = name_size_groups.begin(); it != name_size_groups.end(); ++it) {
        if (it.value().size() < 2) continue;

        DuplicateGroup group;
        group.key = it.key();
        group.file_indices = it.value();
        groups_.push_back(group);

        // Create tree group header
        auto* group_item = new QTreeWidgetItem();
        const auto& first_file = files_[it.value().first()];
        group_item->setText(0, QString("%1 (%2 copies)")
                                .arg(first_file.name).arg(it.value().size()));
        group_item->setFlags(group_item->flags() & ~Qt::ItemIsSelectable);
        group_item->setForeground(0, QColor("#f39c12"));

        // Add child items for each file in group
        for (int fi : it.value()) {
            const auto& file = files_[fi];
            auto* child = new QTreeWidgetItem();
            child->setText(0, file.name);

            // Format size
            QString size_str;
            if (file.size < 1024LL) size_str = QString("%1 B").arg(file.size);
            else if (file.size < 1024LL*1024) size_str = QString("%1 KB").arg(file.size/1024.0, 0, 'f', 1);
            else size_str = QString("%1 MB").arg(file.size/(1024.0*1024.0), 0, 'f', 1);
            child->setText(1, size_str);
            child->setText(2, file.modified_date);
            child->setText(3, file.path);
            child->setData(0, Qt::UserRole, fi);
            child->setToolTip(0, file.path);

            group_item->addChild(child);
            ++total_dupes;
        }

        tree_->addTopLevelItem(group_item);
        group_item->setExpanded(true);
    }

    tree_->resizeColumnToContents(0);
    tree_->resizeColumnToContents(1);
    tree_->resizeColumnToContents(2);

    status_label_->setText(QString("%1 duplicate groups found (%2 total files)")
                           .arg(groups_.size()).arg(total_dupes));
}

QByteArray DuplicateDetectionWindow::compute_file_hash(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    // Read in 64KB chunks for efficiency
    while (!file.atEnd()) {
        hasher.addData(file.read(65536));
    }
    return hasher.result();
}

void DuplicateDetectionWindow::on_verify_with_hash() {
    verify_btn_->setEnabled(false);
    verify_btn_->setText("Hashing...");
    QApplication::processEvents();

    // Re-group by content hash
    QMap<QByteArray, QList<int>> hash_groups;
    int processed = 0;

    for (const auto& group : groups_) {
        for (int fi : group.file_indices) {
            QByteArray hash = compute_file_hash(files_[fi].path);
            if (!hash.isEmpty()) {
                hash_groups[hash].append(fi);
            }
            ++processed;
        }
    }

    // Rebuild tree with hash-verified groups
    groups_.clear();
    tree_->clear();
    int total_dupes = 0;

    for (auto it = hash_groups.begin(); it != hash_groups.end(); ++it) {
        if (it.value().size() < 2) continue;

        DuplicateGroup group;
        group.key = QString(it.key().toHex());
        group.file_indices = it.value();
        groups_.push_back(group);

        auto* group_item = new QTreeWidgetItem();
        const auto& first_file = files_[it.value().first()];
        group_item->setText(0, QString("%1 (%2 identical copies, SHA-256 verified)")
                                .arg(first_file.name).arg(it.value().size()));
        group_item->setFlags(group_item->flags() & ~Qt::ItemIsSelectable);
        group_item->setForeground(0, QColor("#2ecc71"));

        for (int fi : it.value()) {
            const auto& file = files_[fi];
            auto* child = new QTreeWidgetItem();
            child->setText(0, file.name);
            QString size_str;
            if (file.size < 1024LL) size_str = QString("%1 B").arg(file.size);
            else if (file.size < 1024LL*1024) size_str = QString("%1 KB").arg(file.size/1024.0, 0, 'f', 1);
            else size_str = QString("%1 MB").arg(file.size/(1024.0*1024.0), 0, 'f', 1);
            child->setText(1, size_str);
            child->setText(2, file.modified_date);
            child->setText(3, file.path);
            child->setData(0, Qt::UserRole, fi);
            child->setToolTip(0, file.path);
            group_item->addChild(child);
            ++total_dupes;
        }

        tree_->addTopLevelItem(group_item);
        group_item->setExpanded(true);
    }

    tree_->resizeColumnToContents(0);
    verify_btn_->setText("Verified (SHA-256)");
    status_label_->setText(QString("%1 hash-verified duplicate groups (%2 total files)")
                           .arg(groups_.size()).arg(total_dupes));
}

void DuplicateDetectionWindow::on_delete_selected() {
    QList<int> to_delete;
    for (auto* item : tree_->selectedItems()) {
        if (!item->parent()) continue; // Skip group headers
        int fi = item->data(0, Qt::UserRole).toInt();
        to_delete.append(fi);
    }

    if (to_delete.isEmpty()) return;

    auto reply = QMessageBox::question(this, "Confirm Delete",
        QString("Mark %1 file(s) for deletion?\n\nFiles will be deleted when you execute decisions.")
        .arg(to_delete.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        emit files_deleted(to_delete);
        // Update tree: mark deleted items
        for (auto* item : tree_->selectedItems()) {
            if (!item->parent()) continue;
            item->setForeground(0, QColor("#e74c3c"));
            item->setText(0, item->text(0) + " [marked for delete]");
            item->setSelected(false);
            item->setDisabled(true);
        }
    }
}

void DuplicateDetectionWindow::on_keep_newest() {
    tree_->clearSelection();

    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        auto* group_item = tree_->topLevelItem(g);
        if (group_item->childCount() < 2) continue;

        // Find the child with the newest modification date
        int newest_idx = 0;
        QDateTime newest_dt;
        for (int c = 0; c < group_item->childCount(); ++c) {
            auto* child = group_item->child(c);
            if (child->isDisabled()) continue;
            int fi = child->data(0, Qt::UserRole).toInt();
            if (fi >= 0 && fi < static_cast<int>(files_.size())) {
                const QDateTime& dt = files_[fi].modified_datetime;
                if (!newest_dt.isValid() || dt > newest_dt) {
                    newest_dt = dt;
                    newest_idx = c;
                }
            }
        }

        // Select all children except the newest
        for (int c = 0; c < group_item->childCount(); ++c) {
            auto* child = group_item->child(c);
            if (child->isDisabled()) continue;
            if (c != newest_idx) {
                child->setSelected(true);
            }
        }
    }
}
