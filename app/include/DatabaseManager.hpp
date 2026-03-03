#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <vector>
#include <memory>

struct FileDecision {
    QString file_path;
    QString decision;  // "keep", "delete", "sort_later", "move"
    QString destination_folder;
    qint64 timestamp;
};

struct FolderTreeEntry {
    QString folder_path;
    QString display_name;
    QString parent_path;
    bool is_virtual;
    bool is_pinned;
    int sort_order;
};

struct FolderConnection {
    int group_id;
    QString folder_path;
};

class DatabaseManager {
public:
    explicit DatabaseManager(const QString& db_path = "");
    ~DatabaseManager();
    
    bool initialize();
    bool is_open() const;
    
    // File Tinder state management
    bool save_file_decision(const QString& session_folder, const QString& file_path, 
                           const QString& decision, const QString& destination = "");
    std::vector<FileDecision> get_session_decisions(const QString& session_folder);
    bool clear_session(const QString& session_folder);
    FileDecision get_file_decision(const QString& session_folder, const QString& file_path);
    int get_session_progress_count(const QString& session_folder);
    
    // Folder tree management
    bool save_folder_tree_entry(const QString& session_folder, const FolderTreeEntry& entry);
    std::vector<FolderTreeEntry> get_folder_tree(const QString& session_folder);
    bool remove_folder_tree_entry(const QString& session_folder, const QString& folder_path);
    bool update_folder_pinned(const QString& session_folder, const QString& folder_path, bool pinned);
    
    // Folder connections
    bool add_folder_connection(const QString& session_folder, int group_id, const QString& folder_path);
    std::vector<FolderConnection> get_folder_connections(const QString& session_folder);
    bool remove_folder_connection(const QString& session_folder, const QString& folder_path);
    int get_next_connection_group_id(const QString& session_folder);
    
    // Recent folders
    bool add_recent_folder(const QString& folder_path);
    bool remove_recent_folder(const QString& folder_path);
    QStringList get_recent_folders(int limit = 10);
    
    // Quick access folders (manual, limited to 10)
    bool save_quick_access_folders(const QString& session_folder, const QStringList& folders);
    QStringList get_quick_access_folders(const QString& session_folder);
    
    // Execution log (for undo support)
    bool save_execution_log(const QString& session_folder, const QString& action,
                           const QString& source_path, const QString& dest_path);
    std::vector<std::tuple<int, QString, QString, QString, QString>> get_execution_log(const QString& session_folder);
    bool remove_execution_log_entry(int id);
    bool clear_execution_log(const QString& session_folder);
    
    // Grid configuration save/load
    bool save_grid_config(const QString& session_folder, const QString& config_name,
                         const QStringList& folder_paths);
    QStringList get_grid_config(const QString& session_folder, const QString& config_name);
    QStringList get_grid_config_names(const QString& session_folder);
    bool delete_grid_config(const QString& session_folder, const QString& config_name);
    
    // AI provider settings
    bool save_ai_provider(const QString& provider_name, const QString& api_key,
                         const QString& endpoint_url, const QString& model_name,
                         bool is_local, int rate_limit_rpm);
    bool get_ai_provider(const QString& provider_name, QString& api_key,
                        QString& endpoint_url, QString& model_name,
                        bool& is_local, int& rate_limit_rpm);
    QStringList get_ai_provider_names();
    
    // Maintenance
    int cleanup_stale_sessions(int days_old = 30);
    
private:
    QSqlDatabase db_;
    QString db_path_;
    QString connection_name_;
    
    bool create_tables();
    bool execute_query(const QString& query);
};

#endif // DATABASE_MANAGER_HPP
