#ifndef DUPLICATE_DETECTION_WINDOW_HPP
#define DUPLICATE_DETECTION_WINDOW_HPP

#include <QDialog>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <QCryptographicHash>
#include <vector>

struct FileToProcess;

// Group of duplicate files
struct DuplicateGroup {
    QString key;             // Grouping key (name+size or hash)
    QList<int> file_indices; // Indices into files_ vector
};

// Separate duplicate detection window.
// Accessed via ! button on file selector when duplicates detected.
// Shows duplicate groups, allows multi-select and batch delete.
// Uses sophisticated detection: name+size for initial grouping,
// SHA-256 content hashing for verification.
class DuplicateDetectionWindow : public QDialog {
    Q_OBJECT

public:
    explicit DuplicateDetectionWindow(std::vector<FileToProcess>& files,
                                       const QString& source_folder,
                                       QWidget* parent = nullptr);

signals:
    void files_deleted(const QList<int>& file_indices);

private:
    void build_ui();
    void detect_duplicates();
    QByteArray compute_file_hash(const QString& path) const;
    void on_delete_selected();
    void on_verify_with_hash();
    void on_keep_newest();

    std::vector<FileToProcess>& files_;
    QString source_folder_;
    std::vector<DuplicateGroup> groups_;

    QTreeWidget* tree_;
    QLabel* status_label_;
    QPushButton* delete_btn_;
    QPushButton* verify_btn_;
};

#endif // DUPLICATE_DETECTION_WINDOW_HPP
