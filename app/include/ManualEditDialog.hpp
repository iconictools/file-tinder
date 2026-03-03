#ifndef MANUAL_EDIT_DIALOG_HPP
#define MANUAL_EDIT_DIALOG_HPP

#include <QDialog>
#include <QTreeWidget>
#include <QTextEdit>
#include <QStackedWidget>
#include <QPushButton>
#include <QCheckBox>

class ManualEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManualEditDialog(const QString& source_folder,
                              const QStringList& initial_folders = {},
                              QWidget* parent = nullptr);

    QStringList get_folders() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setup_ui();
    void sync_tree_to_text();
    void sync_text_to_tree();
    void add_folder();
    void add_subfolder();
    void delete_selected();
    void populate_tree(const QStringList& folders);
    QStringList tree_to_list() const;
    void collect_paths(QTreeWidgetItem* item, const QString& prefix, QStringList& out) const;

    QString source_folder_;
    QStackedWidget* stack_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTextEdit* text_edit_ = nullptr;
    QCheckBox* view_toggle_ = nullptr;
    QPushButton* add_btn_ = nullptr;
    QPushButton* add_sub_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
};

#endif // MANUAL_EDIT_DIALOG_HPP
