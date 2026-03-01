// FilterWidget.hpp
// Shared filter and sort component for File Tinder

#ifndef FILTER_WIDGET_HPP
#define FILTER_WIDGET_HPP

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QListWidget>
#include <QFrame>

// Forward declare - actual enums are in StandaloneFileTinderDialog.hpp
enum class FileFilterType;
enum class SortOrder;

// Sort field options (FilterWidget specific)
enum class SortField {
    Name = 0,
    Size,
    Type,
    DateModified
};

// Dialog for specifying custom extensions
class CustomExtensionDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomExtensionDialog(QWidget* parent = nullptr);
    QStringList get_extensions() const;
    void set_extensions(const QStringList& extensions);

private:
    QLineEdit* extension_input_;
    QListWidget* extension_list_;
    QPushButton* add_btn_;
    QPushButton* remove_btn_;
    QPushButton* ok_btn_;
    QPushButton* cancel_btn_;
};

// Main filter widget - shared between Basic and Advanced modes
class FilterWidget : public QWidget {
    Q_OBJECT

public:
    explicit FilterWidget(QWidget* parent = nullptr);

    // Getters
    FileFilterType get_filter_type() const;
    SortField get_sort_field() const;
    SortOrder get_sort_order() const;
    bool get_include_folders() const;
    QStringList get_custom_extensions() const;

    // Setters
    void set_filter_type(FileFilterType type);
    void set_sort_field(SortField field);
    void set_sort_order(SortOrder order);
    void set_include_folders(bool include);
    void set_custom_extensions(const QStringList& extensions);

signals:
    void filter_changed();
    void sort_changed();
    void include_folders_changed(bool include);

private slots:
    void on_filter_changed(int index);
    void on_sort_field_changed(int index);
    void on_sort_order_toggled();
    void on_include_folders_toggled(bool checked);
    void on_specify_clicked();

private:
    void setup_ui();

    QComboBox* filter_combo_;
    QComboBox* sort_combo_;
    QPushButton* sort_order_btn_;
    QCheckBox* include_folders_check_;
    
    FileFilterType current_filter_;
    SortField current_sort_field_;
    SortOrder current_sort_order_;
    QStringList custom_extensions_;
};

#endif // FILTER_WIDGET_HPP
