// FilterWidget.cpp
// Shared filter and sort component implementation

#include "FilterWidget.hpp"
#include "StandaloneFileTinderDialog.hpp"  // For FileFilterType and SortOrder enums
#include "ui_constants.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFrame>
#include <QRegularExpression>

// ============================================================================
// CustomExtensionDialog
// ============================================================================

CustomExtensionDialog::CustomExtensionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Custom File Extensions");
    setMinimumSize(ui::scaling::scaled(300), ui::scaling::scaled(250));

    auto* layout = new QVBoxLayout(this);

    // Instructions
    auto* instructions = new QLabel("Enter file extensions (without dot):");
    layout->addWidget(instructions);

    // Input row
    auto* input_row = new QHBoxLayout();
    extension_input_ = new QLineEdit();
    extension_input_->setPlaceholderText("e.g., txt, pdf, docx");
    add_btn_ = new QPushButton("Add");
    add_btn_->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 4px 12px; border-radius: 3px; }"
                          "QPushButton:hover { background-color: #2980b9; }");
    input_row->addWidget(extension_input_);
    input_row->addWidget(add_btn_);
    layout->addLayout(input_row);

    // Extension list
    extension_list_ = new QListWidget();
    extension_list_->setStyleSheet("QListWidget { border: 1px solid #555; border-radius: 3px; }");
    layout->addWidget(extension_list_);

    // Remove button
    remove_btn_ = new QPushButton("Remove Selected");
    layout->addWidget(remove_btn_);

    // Dialog buttons
    auto* button_row = new QHBoxLayout();
    ok_btn_ = new QPushButton("OK");
    cancel_btn_ = new QPushButton("Cancel");
    button_row->addStretch();
    button_row->addWidget(ok_btn_);
    button_row->addWidget(cancel_btn_);
    layout->addLayout(button_row);

    // Connections
    connect(add_btn_, &QPushButton::clicked, this, [this]() {
        QString ext = extension_input_->text().trimmed().toLower();
        if (ext.isEmpty()) return;

        if (ext.contains(' ') || ext.length() > 10) {
            return; // Invalid extension
        }

        QStringList parts = ext.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        for (QString& part : parts) {
            part = part.trimmed();
            if (part.startsWith('.')) part = part.mid(1);
            if (!part.isEmpty() && part.length() <= 10 && !part.contains(' ')) {
                // Check for duplicates
                bool found = false;
                for (int i = 0; i < extension_list_->count(); ++i) {
                    if (extension_list_->item(i)->text() == part) { found = true; break; }
                }
                if (!found) extension_list_->addItem(part);
            }
        }
        extension_input_->clear();
    });

    connect(extension_input_, &QLineEdit::returnPressed, add_btn_, &QPushButton::click);

    connect(remove_btn_, &QPushButton::clicked, this, [this]() {
        auto* item = extension_list_->currentItem();
        if (item) {
            delete extension_list_->takeItem(extension_list_->row(item));
        }
    });

    connect(ok_btn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_btn_, &QPushButton::clicked, this, &QDialog::reject);
}

QStringList CustomExtensionDialog::get_extensions() const {
    QStringList extensions;
    for (int i = 0; i < extension_list_->count(); ++i) {
        extensions.append(extension_list_->item(i)->text());
    }
    return extensions;
}

void CustomExtensionDialog::set_extensions(const QStringList& extensions) {
    extension_list_->clear();
    for (const auto& ext : extensions) {
        extension_list_->addItem(ext);
    }
}

// ============================================================================
// FilterWidget
// ============================================================================

FilterWidget::FilterWidget(QWidget* parent)
    : QWidget(parent)
    , current_filter_(FileFilterType::All)
    , current_sort_field_(SortField::Name)
    , current_sort_order_(SortOrder::Ascending)
{
    setup_ui();
}

void FilterWidget::setup_ui() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    // Filter section
    auto* filter_label = new QLabel("Filter:");
    layout->addWidget(filter_label);

    filter_combo_ = new QComboBox();
    filter_combo_->addItem("All", static_cast<int>(FileFilterType::All));
    filter_combo_->addItem("Images", static_cast<int>(FileFilterType::Images));
    filter_combo_->addItem("Videos", static_cast<int>(FileFilterType::Videos));
    filter_combo_->addItem("Audio", static_cast<int>(FileFilterType::Audio));
    filter_combo_->addItem("Documents", static_cast<int>(FileFilterType::Documents));
    filter_combo_->addItem("Archives", static_cast<int>(FileFilterType::Archives));
    filter_combo_->addItem("Other", static_cast<int>(FileFilterType::Other));
    filter_combo_->addItem("Folders Only", static_cast<int>(FileFilterType::FoldersOnly));
    filter_combo_->addItem("Specify...", static_cast<int>(FileFilterType::Custom));
    filter_combo_->setMinimumWidth(100);
    layout->addWidget(filter_combo_);

    // Clear filter button
    auto* clear_filter_btn = new QPushButton("X");
    clear_filter_btn->setMaximumWidth(ui::scaling::scaled(24));
    clear_filter_btn->setMaximumHeight(ui::scaling::scaled(24));
    clear_filter_btn->setToolTip("Clear filter");
    clear_filter_btn->setStyleSheet("QPushButton { font-size: 10px; color: #e74c3c; border: 1px solid #555; border-radius: 3px; padding: 0; }"
                                    "QPushButton:hover { background-color: #5d3a37; }");
    connect(clear_filter_btn, &QPushButton::clicked, this, [this]() {
        filter_combo_->setCurrentIndex(0);
    });
    layout->addWidget(clear_filter_btn);

    // Subfolder options — stacked vertically beside filter
    auto* subfolder_col = new QVBoxLayout();
    subfolder_col->setContentsMargins(0, 0, 0, 0);
    subfolder_col->setSpacing(1);

    include_folders_check_ = new QCheckBox("Include Folders");
    include_folders_check_->setStyleSheet("font-size: 11px;");
    subfolder_col->addWidget(include_folders_check_);

    auto* depth_row = new QHBoxLayout();
    depth_row->setContentsMargins(0, 0, 0, 0);
    depth_row->setSpacing(4);
    auto* depth_label = new QLabel("Depth:");
    depth_label->setStyleSheet("font-size: 11px; color: #95a5a6;");
    depth_row->addWidget(depth_label);
    subfolder_depth_spin_ = new QSpinBox();
    subfolder_depth_spin_->setRange(0, 5);
    subfolder_depth_spin_->setValue(1);
    subfolder_depth_spin_->setMaximumWidth(ui::scaling::scaled(45));
    subfolder_depth_spin_->setToolTip("How many levels of subfolders to include (0 = top-level only)");
    subfolder_depth_spin_->setEnabled(false);
    depth_row->addWidget(subfolder_depth_spin_);
    depth_row->addStretch();
    subfolder_col->addLayout(depth_row);

    layout->addLayout(subfolder_col);

    // Visual separator
    auto* separator = new QFrame();
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setStyleSheet("color: #555;");
    layout->addWidget(separator);

    // Sort section
    auto* sort_label = new QLabel("Sort:");
    layout->addWidget(sort_label);

    sort_combo_ = new QComboBox();
    sort_combo_->addItem("Name", static_cast<int>(SortField::Name));
    sort_combo_->addItem("Size", static_cast<int>(SortField::Size));
    sort_combo_->addItem("Type", static_cast<int>(SortField::Type));
    sort_combo_->addItem("Date Modified", static_cast<int>(SortField::DateModified));
    sort_combo_->setMinimumWidth(100);
    layout->addWidget(sort_combo_);

    // Sort order button
    sort_order_btn_ = new QPushButton("Asc");
    sort_order_btn_->setMinimumWidth(ui::scaling::scaled(50));
    sort_order_btn_->setCheckable(true);
    sort_order_btn_->setToolTip("Toggle Ascending/Descending");
    sort_order_btn_->setStyleSheet("QPushButton { padding: 3px 8px; border: 1px solid #555; border-radius: 3px; }"
                                   "QPushButton:hover { background-color: #3d566e; }");
    layout->addWidget(sort_order_btn_);

    layout->addStretch();

    // Connections
    connect(filter_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FilterWidget::on_filter_changed);
    connect(sort_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FilterWidget::on_sort_field_changed);
    connect(sort_order_btn_, &QPushButton::toggled,
            this, &FilterWidget::on_sort_order_toggled);
    connect(include_folders_check_, &QCheckBox::toggled,
            this, &FilterWidget::on_include_folders_toggled);
    connect(include_folders_check_, &QCheckBox::toggled,
            subfolder_depth_spin_, &QSpinBox::setEnabled);
    connect(subfolder_depth_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int depth) { emit subfolder_depth_changed(depth); });

    // Active filter highlight
    connect(filter_combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (text == "All Files" || text == "All") {
            filter_combo_->setStyleSheet("");
        } else {
            filter_combo_->setStyleSheet("QComboBox { border: 2px solid #3498db; background-color: #1a3a5c; }");
        }
    });
}

void FilterWidget::on_filter_changed(int index) {
    auto type = static_cast<FileFilterType>(filter_combo_->itemData(index).toInt());
    
    if (type == FileFilterType::Custom) {
        on_specify_clicked();
        return;
    }

    current_filter_ = type;
    
    // Disable "include folders" when "Folders Only" is selected
    include_folders_check_->setEnabled(type != FileFilterType::FoldersOnly);
    
    emit filter_changed();
}

void FilterWidget::on_sort_field_changed(int index) {
    current_sort_field_ = static_cast<SortField>(sort_combo_->itemData(index).toInt());
    emit sort_changed();
}

void FilterWidget::on_sort_order_toggled() {
    if (sort_order_btn_->isChecked()) {
        current_sort_order_ = SortOrder::Descending;
        sort_order_btn_->setText("Desc");
    } else {
        current_sort_order_ = SortOrder::Ascending;
        sort_order_btn_->setText("Asc");
    }
    emit sort_changed();
}

void FilterWidget::on_include_folders_toggled(bool checked) {
    emit include_folders_changed(checked);
}

void FilterWidget::on_specify_clicked() {
    CustomExtensionDialog dialog(this);
    dialog.set_extensions(custom_extensions_);
    
    if (dialog.exec() == QDialog::Accepted) {
        custom_extensions_ = dialog.get_extensions();
        if (!custom_extensions_.isEmpty()) {
            current_filter_ = FileFilterType::Custom;
            emit filter_changed();
        } else {
            // If no extensions specified, revert to All
            filter_combo_->setCurrentIndex(0);
        }
    } else {
        // Revert to previous filter
        int prev_index = filter_combo_->findData(static_cast<int>(current_filter_));
        if (prev_index >= 0) {
            filter_combo_->blockSignals(true);
            filter_combo_->setCurrentIndex(prev_index);
            filter_combo_->blockSignals(false);
        }
    }
}

FileFilterType FilterWidget::get_filter_type() const {
    return current_filter_;
}

SortField FilterWidget::get_sort_field() const {
    return current_sort_field_;
}

SortOrder FilterWidget::get_sort_order() const {
    return current_sort_order_;
}

bool FilterWidget::get_include_folders() const {
    return include_folders_check_->isChecked();
}

QStringList FilterWidget::get_custom_extensions() const {
    return custom_extensions_;
}

void FilterWidget::set_filter_type(FileFilterType type) {
    current_filter_ = type;
    int index = filter_combo_->findData(static_cast<int>(type));
    if (index >= 0) {
        filter_combo_->setCurrentIndex(index);
    }
}

void FilterWidget::set_sort_field(SortField field) {
    current_sort_field_ = field;
    int index = sort_combo_->findData(static_cast<int>(field));
    if (index >= 0) {
        sort_combo_->setCurrentIndex(index);
    }
}

void FilterWidget::set_sort_order(SortOrder order) {
    current_sort_order_ = order;
    sort_order_btn_->setChecked(order == SortOrder::Descending);
}

void FilterWidget::set_include_folders(bool include) {
    include_folders_check_->setChecked(include);
}

void FilterWidget::set_custom_extensions(const QStringList& extensions) {
    custom_extensions_ = extensions;
}

int FilterWidget::get_subfolder_depth() const {
    return subfolder_depth_spin_->value();
}

void FilterWidget::set_subfolder_depth(int depth) {
    subfolder_depth_spin_->setValue(depth);
}
