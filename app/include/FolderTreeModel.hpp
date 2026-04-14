#ifndef FOLDER_TREE_MODEL_HPP
#define FOLDER_TREE_MODEL_HPP

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>
#include <vector>
#include <memory>

class DatabaseManager;

struct FolderNode {
    QString path;
    QString display_name;
    bool exists;                      // Existing folder vs. to-be-created
    bool is_pinned;                   // Pinned to quick access
    bool is_connected;                // Part of a connection group
    bool is_external;                 // Folder outside the source folder
    int connection_group_id;          // Connection group ID
    int assigned_file_count;
    QString custom_color;             // User-assigned color (hex, empty = default)
    FolderNode* parent;
    std::vector<std::unique_ptr<FolderNode>> children;
    
    FolderNode() : exists(true), is_pinned(false), is_connected(false), 
                   is_external(false), connection_group_id(-1), assigned_file_count(0), parent(nullptr) {}
};

class FolderTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        DisplayNameRole,
        ExistsRole,
        IsPinnedRole,
        IsConnectedRole,
        IsExternalRole,
        ConnectionGroupRole,
        FileCountRole
    };

    explicit FolderTreeModel(QObject* parent = nullptr);
    ~FolderTreeModel() override;
    
    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // Folder tree operations
    void set_root_folder(const QString& path);
    QString root_folder() const { return root_ ? root_->path : QString(); }
    void add_folder(const QString& path, bool virtual_folder = false);
    void remove_folder(const QString& path);
    void set_folder_pinned(const QString& path, bool pinned);
    
    // Connection management
    void connect_folders(const QStringList& paths, int group_id = -1);
    void disconnect_folder(const QString& path);
    
    // File assignment
    void assign_file_to_folder(const QString& folder_path);
    void unassign_file_from_folder(const QString& folder_path);
    void clear_assignments();
    
    // Getters
    FolderNode* node_at(const QModelIndex& index) const;
    FolderNode* find_node(const QString& path) const;
    QStringList get_virtual_folders() const;
    QStringList get_pinned_folders() const;
    QStringList get_connected_folders(int group_id) const;
    QStringList get_all_folder_paths() const;  // All non-root folder paths in tree
    QModelIndex index_for_path(const QString& path) const;
    FolderNode* root_node() const { return root_.get(); }
    
    // Sorting
    void sort_children_alphabetically(FolderNode* node);
    void sort_children_by_count(FolderNode* node);
    
    // Persistence
    void load_from_database(DatabaseManager& db, const QString& session_folder);
    void save_to_database(DatabaseManager& db, const QString& session_folder);
    
signals:
    void folder_assigned(const QString& folder_path);
    void folder_structure_changed();
    
private:
    std::unique_ptr<FolderNode> root_;
    int next_connection_group_id_;
    
    void scan_directory(FolderNode* parent, const QString& path, int depth = 0);
    FolderNode* find_node_recursive(FolderNode* node, const QString& path) const;
    QModelIndex index_for_node(FolderNode* node) const;
    void collect_virtual_folders(FolderNode* node, QStringList& result) const;
};

#endif // FOLDER_TREE_MODEL_HPP
