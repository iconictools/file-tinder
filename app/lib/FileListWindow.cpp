#include "FileListWindow.hpp"
#include "StandaloneFileTinderDialog.hpp"
#include "ui_constants.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDrag>
#include <QMimeData>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QApplication>
#include <QMouseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QLocale>
#include <QDataStream>

static const int kFileIndexRole = Qt::UserRole + 200;

// Custom list widget that provides file-index MIME data for drag operations
class DragListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* data = new QMimeData();
        QByteArray encoded;
        QDataStream stream(&encoded, QIODevice::WriteOnly);
        for (auto* item : items) {
            stream << item->data(kFileIndexRole).toInt();
        }
        data->setData("application/x-filetinder-indices", encoded);
        return data;
    }
    QStringList mimeTypes() const override {
        return {"application/x-filetinder-indices"};
    }
    Qt::DropActions supportedDropActions() const override {
        return Qt::MoveAction;
    }
};

FileListWindow::FileListWindow(std::vector<FileToProcess>& files,
                               const std::vector<int>& filtered_indices,
                               int current_index,
                               QWidget* parent)
    : QDialog(parent, Qt::Tool)
    , files_(files)
    , filtered_indices_(filtered_indices)
    , current_index_(current_index)
{
    setWindowTitle("File List");
    build_ui();
    update_list();
}

void FileListWindow::build_ui() {
    setMinimumSize(ui::scaling::scaled(280), ui::scaling::scaled(320));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Filter bar
    auto* filter_row = new QHBoxLayout();
    filter_edit_ = new QLineEdit();
    filter_edit_->setPlaceholderText("Filter files...");
    filter_edit_->setStyleSheet("padding: 5px 8px;");
    connect(filter_edit_, &QLineEdit::textChanged, this, &FileListWindow::on_filter_changed);
    filter_row->addWidget(filter_edit_);

    group_by_decision_ = new QCheckBox("Group by decision");
    group_by_decision_->setToolTip("Group files by their decision status");
    connect(group_by_decision_, &QCheckBox::toggled, this, [this]() { update_list(); });
    filter_row->addWidget(group_by_decision_);

    layout->addLayout(filter_row);

    // File list (DragListWidget provides custom MIME data for cross-widget drag)
    list_widget_ = new DragListWidget();
    list_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_widget_->setDragEnabled(true);
    list_widget_->setDragDropMode(QAbstractItemView::DragOnly);
    list_widget_->setStyleSheet(
        "QListWidget::item { padding: 3px 6px; }"
        "QListWidget::item:selected { background-color: palette(highlight); color: palette(highlighted-text); }");
    connect(list_widget_, &QListWidget::itemClicked, this, &FileListWindow::on_item_clicked);
    connect(list_widget_, &QListWidget::itemDoubleClicked, this, &FileListWindow::on_item_double_clicked);

    // Context menu for assigning selected files
    list_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_widget_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto selected = list_widget_->selectedItems();
        if (selected.isEmpty()) return;

        QMenu menu(this);

        // Decision changes
        auto* dec_header = menu.addAction("Set decision:");
        dec_header->setEnabled(false);
        auto* keep_action = menu.addAction("Keep");
        keep_action->setData("keep");
        auto* delete_action = menu.addAction("Delete");
        delete_action->setData("delete");
        auto* sort_later_action = menu.addAction("Sort Later");
        sort_later_action->setData("sort_later");
        menu.addSeparator();

        // Folder assignment
        if (!destination_folders_.isEmpty()) {
            auto* assign_header = menu.addAction("Move to folder:");
            assign_header->setEnabled(false);
            for (const QString& folder : destination_folders_) {
                QString display = QFileInfo(folder).fileName();
                if (display.isEmpty()) display = folder;
                auto* action = menu.addAction("  " + display);
                action->setData("move:" + folder);
                action->setToolTip(folder);
            }
        }

        auto* chosen = menu.exec(list_widget_->viewport()->mapToGlobal(pos));
        if (!chosen || !chosen->data().isValid()) return;

        QList<int> indices;
        for (auto* item : selected) {
            int fi = item->data(kFileIndexRole).toInt();
            indices.append(fi);
        }

        QString data = chosen->data().toString();
        if (data.startsWith("move:")) {
            emit files_assigned(indices, data.mid(5));
        } else {
            emit files_decision_changed(indices, data);
        }
    });

    layout->addWidget(list_widget_, 1);

    // Status bar
    auto* status_row = new QHBoxLayout();
    count_label_ = new QLabel();
    count_label_->setStyleSheet("font-size: 10px;");
    status_row->addWidget(count_label_);
    status_row->addStretch();
    selection_label_ = new QLabel();
    selection_label_->setStyleSheet("font-size: 10px;");
    status_row->addWidget(selection_label_);
    layout->addLayout(status_row);

    // Tip
    auto* tip = new QLabel("Click: navigate | Ctrl/Shift+Click: multi-select | Right-click: assign to folder");
    tip->setStyleSheet("font-size: 9px;");
    tip->setWordWrap(true);
    layout->addWidget(tip);
}

void FileListWindow::refresh(const std::vector<int>& filtered_indices, int current_index) {
    filtered_indices_ = filtered_indices;
    current_index_ = current_index;
    update_list();
}

void FileListWindow::set_destination_folders(const QStringList& folders) {
    destination_folders_ = folders;
}

void FileListWindow::update_list() {
    list_widget_->clear();
    QString filter_text = filter_edit_->text().toLower();

    int shown = 0;
    for (int i = 0; i < static_cast<int>(filtered_indices_.size()); ++i) {
        int fi = filtered_indices_[i];
        if (fi < 0 || fi >= static_cast<int>(files_.size())) continue;
        const auto& file = files_[fi];

        // Apply text filter
        if (!filter_text.isEmpty() && !file.name.toLower().contains(filter_text)) continue;

        // Build display text
        QString status;
        if (file.decision == "pending") status = "[ ]";
        else if (file.decision == "keep") status = "[K]";
        else if (file.decision == "delete") status = "[D]";
        else if (file.decision == "sort_later") status = "[L]";
        else if (file.decision == "move") status = "[M]";
        else if (file.decision == "copy") status = "[C]";
        else status = "[?]";

        QString display = QString("%1 %2 (%3)").arg(status, file.name,
            QLocale().formattedDataSize(file.size, 1, QLocale::DataSizeTraditionalFormat));

        auto* item = new QListWidgetItem(display);
        item->setData(kFileIndexRole, fi);
        item->setData(Qt::UserRole + 201, i);  // filtered index

        // Color by decision
        if (file.decision == "keep") item->setForeground(QColor("#2ecc71"));
        else if (file.decision == "delete") item->setForeground(QColor("#e74c3c"));
        else if (file.decision == "sort_later") item->setForeground(QColor("#95a5a6"));
        else if (file.decision == "move") item->setForeground(QColor("#3498db"));
        else if (file.decision == "copy") item->setForeground(QColor("#9b59b6"));

        // Highlight current file
        if (i == current_index_) {
            item->setBackground(QColor("#2c3e50"));
            auto font = item->font();
            font.setBold(true);
            item->setFont(font);
        }

        item->setToolTip(file.path);
        list_widget_->addItem(item);
        ++shown;
    }

    // Sort based on group-by-decision checkbox
    if (group_by_decision_ && group_by_decision_->isChecked()) {
        QList<QListWidgetItem*> items;
        while (list_widget_->count())
            items.append(list_widget_->takeItem(0));
        std::stable_sort(items.begin(), items.end(), [this](QListWidgetItem* a, QListWidgetItem* b) {
            int fi_a = a->data(kFileIndexRole).toInt();
            int fi_b = b->data(kFileIndexRole).toInt();
            return files_[fi_a].decision < files_[fi_b].decision;
        });
        for (auto* it : items)
            list_widget_->addItem(it);
    }

    count_label_->setText(QString("%1 / %2 files").arg(shown).arg(filtered_indices_.size()));
    selection_label_->setText("0 selected");

    connect(list_widget_, &QListWidget::itemSelectionChanged, this, [this]() {
        int sel = list_widget_->selectedItems().size();
        selection_label_->setText(QString("%1 selected").arg(sel));
    });
}

void FileListWindow::on_filter_changed(const QString&) {
    update_list();
}

void FileListWindow::on_item_clicked(QListWidgetItem* item) {
    if (!item) return;
    // Only navigate on single-click without modifier keys
    if (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
        return;
    int filtered_idx = item->data(Qt::UserRole + 201).toInt();
    emit file_selected(filtered_idx);
}

void FileListWindow::update_item_status(int file_index) {
    if (!list_widget_) return;
    if (file_index < 0 || file_index >= static_cast<int>(files_.size())) return;
    for (int i = 0; i < list_widget_->count(); ++i) {
        auto* item = list_widget_->item(i);
        if (item && item->data(kFileIndexRole).toInt() == file_index) {
            const auto& file = files_[file_index];
            QString status;
            if (file.decision == "keep") status = "[K]";
            else if (file.decision == "delete") status = "[D]";
            else if (file.decision == "sort_later") status = "[L]";
            else if (file.decision == "move") status = "[M]";
            else if (file.decision == "copy") status = "[C]";
            else status = "[ ]";

            QString display = QString("%1 %2 (%3)").arg(status, file.name,
                QLocale().formattedDataSize(file.size, 1, QLocale::DataSizeTraditionalFormat));
            item->setText(display);

            if (file.decision == "keep") item->setForeground(QColor("#2ecc71"));
            else if (file.decision == "delete") item->setForeground(QColor("#e74c3c"));
            else if (file.decision == "sort_later") item->setForeground(QColor("#95a5a6"));
            else if (file.decision == "move") item->setForeground(QColor("#3498db"));
            else if (file.decision == "copy") item->setForeground(QColor("#9b59b6"));
            else item->setForeground(QColor("#bdc3c7"));

            break;
        }
    }
}

void FileListWindow::on_item_double_clicked(QListWidgetItem* item) {
    if (!item) return;
    int filtered_idx = item->data(Qt::UserRole + 201).toInt();
    emit file_selected(filtered_idx);
}
