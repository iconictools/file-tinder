#ifndef FILE_LIST_WINDOW_HPP
#define FILE_LIST_WINDOW_HPP

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <vector>

struct FileToProcess;

// Separate file list window that spans all modes.
// Replaces the "search files" feature with a full file browser:
// - Shows all files in order (filterable)
// - Multi-select with Shift/Ctrl+click
// - Drag-and-drop to grid folders
// - Click to navigate to file in main view
class FileListWindow : public QDialog {
    Q_OBJECT

public:
    explicit FileListWindow(std::vector<FileToProcess>& files,
                            const std::vector<int>& filtered_indices,
                            int current_index,
                            QWidget* parent = nullptr);

    void refresh(const std::vector<int>& filtered_indices, int current_index);
    void set_destination_folders(const QStringList& folders);

signals:
    void file_selected(int filtered_index);
    void files_assigned(const QList<int>& file_indices, const QString& destination);
    void files_decision_changed(const QList<int>& file_indices, const QString& decision);

private:
    void build_ui();
    void update_list();
    void on_filter_changed(const QString& text);
    void on_item_clicked(QListWidgetItem* item);
    void on_item_double_clicked(QListWidgetItem* item);

    std::vector<FileToProcess>& files_;
    std::vector<int> filtered_indices_;
    int current_index_;
    QStringList destination_folders_;

    QLineEdit* filter_edit_ = nullptr;
    QCheckBox* group_by_decision_ = nullptr;
    QListWidget* list_widget_ = nullptr;
    QLabel* count_label_ = nullptr;
    QLabel* selection_label_ = nullptr;
};

#endif // FILE_LIST_WINDOW_HPP
