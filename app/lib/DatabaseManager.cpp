#include "DatabaseManager.hpp"
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>

DatabaseManager::DatabaseManager(const QString& db_path)
    : db_path_(db_path)
    , connection_name_("FileTinderDB_" + QString::number(reinterpret_cast<quintptr>(this))) {
    if (db_path_.isEmpty()) {
        QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(data_dir);
        db_path_ = data_dir + "/file_tinder.db";
    }
}

DatabaseManager::~DatabaseManager() {
    if (db_.isOpen()) {
        db_.close();
    }
    QSqlDatabase::removeDatabase(connection_name_);
}

bool DatabaseManager::initialize() {
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection_name_);
    db_.setDatabaseName(db_path_);
    
    if (!db_.open()) {
        qWarning() << "Failed to open database:" << db_.lastError().text();
        return false;
    }
    
    return create_tables();
}

bool DatabaseManager::is_open() const {
    return db_.isOpen();
}

bool DatabaseManager::create_tables() {
    QStringList queries;
    
    // File Tinder state table
    queries << R"(
        CREATE TABLE IF NOT EXISTS file_tinder_state (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            folder_path TEXT NOT NULL,
            file_path TEXT NOT NULL,
            decision TEXT NOT NULL,
            destination_folder TEXT,
            decided_in_mode TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(folder_path, file_path)
        )
    )";
    
    // Folder tree configuration
    queries << R"(
        CREATE TABLE IF NOT EXISTS tinder_folder_tree (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            folder_path TEXT NOT NULL,
            display_name TEXT,
            is_virtual INTEGER DEFAULT 0,
            is_pinned INTEGER DEFAULT 0,
            parent_path TEXT,
            sort_order INTEGER DEFAULT 0,
            UNIQUE(session_folder, folder_path)
        )
    )";
    
    // Folder connections
    queries << R"(
        CREATE TABLE IF NOT EXISTS tinder_folder_connections (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            group_id INTEGER NOT NULL,
            folder_path TEXT NOT NULL,
            UNIQUE(session_folder, folder_path)
        )
    )";
    
    // Recent folders
    queries << R"(
        CREATE TABLE IF NOT EXISTS recent_folders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            folder_path TEXT NOT NULL UNIQUE,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    // Grid configurations
    queries << R"(
        CREATE TABLE IF NOT EXISTS grid_configs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            config_name TEXT NOT NULL,
            folder_path TEXT NOT NULL,
            sort_order INTEGER NOT NULL
        )
    )";
    
    // AI provider settings
    queries << R"(
        CREATE TABLE IF NOT EXISTS ai_providers (
            provider_name TEXT PRIMARY KEY,
            api_key TEXT,
            endpoint_url TEXT,
            model_name TEXT,
            is_local INTEGER DEFAULT 0,
            rate_limit_rpm INTEGER DEFAULT 60,
            last_used DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    for (const QString& query : queries) {
        if (!execute_query(query)) {
            return false;
        }
    }
    
    // Migration: add decided_in_mode column if it doesn't exist (for existing DBs)
    {
        QSqlQuery q(db_);
        q.exec("ALTER TABLE file_tinder_state ADD COLUMN decided_in_mode TEXT");
        // Ignore error -- column may already exist
    }
    
    // Session source folders table (multi-folder persistence)
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS session_source_folders (
            session_folder TEXT NOT NULL,
            source_path TEXT NOT NULL,
            sort_order INTEGER DEFAULT 0,
            UNIQUE(session_folder, source_path)
        )
    )");
    
    return true;
}

bool DatabaseManager::execute_query(const QString& query) {
    QSqlQuery q(db_);
    if (!q.exec(query)) {
        qWarning() << "Query failed:" << q.lastError().text();
        qWarning() << "Query:" << query;
        return false;
    }
    return true;
}

bool DatabaseManager::save_file_decision(const QString& session_folder, const QString& file_path,
                                         const QString& decision, const QString& destination,
                                         const QString& decided_in_mode) {
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT OR REPLACE INTO file_tinder_state 
        (folder_path, file_path, decision, destination_folder, decided_in_mode, timestamp)
        VALUES (?, ?, ?, ?, ?, datetime('now'))
    )");
    query.addBindValue(session_folder);
    query.addBindValue(file_path);
    query.addBindValue(decision);
    query.addBindValue(destination);
    query.addBindValue(decided_in_mode);
    
    if (!query.exec()) {
        qWarning() << "Failed to save file decision:" << query.lastError().text();
        return false;
    }
    return true;
}

std::vector<FileDecision> DatabaseManager::get_session_decisions(const QString& session_folder) {
    std::vector<FileDecision> decisions;
    
    QSqlQuery query(db_);
    query.prepare(R"(
        SELECT file_path, decision, destination_folder, timestamp, decided_in_mode
        FROM file_tinder_state
        WHERE folder_path = ?
        ORDER BY timestamp
    )");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            FileDecision fd;
            fd.file_path = query.value(0).toString();
            fd.decision = query.value(1).toString();
            fd.destination_folder = query.value(2).toString();
            fd.timestamp = query.value(3).toLongLong();
            fd.decided_in_mode = query.value(4).toString();
            decisions.push_back(fd);
        }
    }
    
    return decisions;
}

bool DatabaseManager::clear_session(const QString& session_folder) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM file_tinder_state WHERE folder_path = ?");
    query.addBindValue(session_folder);
    return query.exec();
}

FileDecision DatabaseManager::get_file_decision(const QString& session_folder, const QString& file_path) {
    FileDecision fd;
    fd.decision = "pending";
    
    QSqlQuery query(db_);
    query.prepare(R"(
        SELECT decision, destination_folder, timestamp, decided_in_mode
        FROM file_tinder_state
        WHERE folder_path = ? AND file_path = ?
    )");
    query.addBindValue(session_folder);
    query.addBindValue(file_path);
    
    if (query.exec() && query.next()) {
        fd.file_path = file_path;
        fd.decision = query.value(0).toString();
        fd.destination_folder = query.value(1).toString();
        fd.timestamp = query.value(2).toLongLong();
        fd.decided_in_mode = query.value(3).toString();
    }
    
    return fd;
}

int DatabaseManager::get_session_progress_count(const QString& session_folder) {
    QSqlQuery query(db_);
    query.prepare("SELECT COUNT(*) FROM file_tinder_state WHERE folder_path = ? AND decision != 'pending'");
    query.addBindValue(session_folder);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool DatabaseManager::save_folder_tree_entry(const QString& session_folder, const FolderTreeEntry& entry) {
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT OR REPLACE INTO tinder_folder_tree
        (session_folder, folder_path, display_name, is_virtual, is_pinned, parent_path, sort_order)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(session_folder);
    query.addBindValue(entry.folder_path);
    query.addBindValue(entry.display_name);
    query.addBindValue(entry.is_virtual ? 1 : 0);
    query.addBindValue(entry.is_pinned ? 1 : 0);
    query.addBindValue(entry.parent_path);
    query.addBindValue(entry.sort_order);
    
    if (!query.exec()) {
        qWarning() << "Failed to save folder tree entry:" << query.lastError().text();
        return false;
    }
    return true;
}

std::vector<FolderTreeEntry> DatabaseManager::get_folder_tree(const QString& session_folder) {
    std::vector<FolderTreeEntry> entries;
    
    QSqlQuery query(db_);
    query.prepare(R"(
        SELECT folder_path, display_name, is_virtual, is_pinned, parent_path, sort_order
        FROM tinder_folder_tree
        WHERE session_folder = ?
        ORDER BY sort_order
    )");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            FolderTreeEntry entry;
            entry.folder_path = query.value(0).toString();
            entry.display_name = query.value(1).toString();
            entry.is_virtual = query.value(2).toBool();
            entry.is_pinned = query.value(3).toBool();
            entry.parent_path = query.value(4).toString();
            entry.sort_order = query.value(5).toInt();
            entries.push_back(entry);
        }
    }
    
    return entries;
}

bool DatabaseManager::remove_folder_tree_entry(const QString& session_folder, const QString& folder_path) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM tinder_folder_tree WHERE session_folder = ? AND folder_path = ?");
    query.addBindValue(session_folder);
    query.addBindValue(folder_path);
    return query.exec();
}

bool DatabaseManager::update_folder_pinned(const QString& session_folder, const QString& folder_path, bool pinned) {
    QSqlQuery query(db_);
    query.prepare("UPDATE tinder_folder_tree SET is_pinned = ? WHERE session_folder = ? AND folder_path = ?");
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(session_folder);
    query.addBindValue(folder_path);
    return query.exec();
}

bool DatabaseManager::add_folder_connection(const QString& session_folder, int group_id, const QString& folder_path) {
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT OR REPLACE INTO tinder_folder_connections
        (session_folder, group_id, folder_path)
        VALUES (?, ?, ?)
    )");
    query.addBindValue(session_folder);
    query.addBindValue(group_id);
    query.addBindValue(folder_path);
    return query.exec();
}

std::vector<FolderConnection> DatabaseManager::get_folder_connections(const QString& session_folder) {
    std::vector<FolderConnection> connections;
    
    QSqlQuery query(db_);
    query.prepare("SELECT group_id, folder_path FROM tinder_folder_connections WHERE session_folder = ?");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            FolderConnection conn;
            conn.group_id = query.value(0).toInt();
            conn.folder_path = query.value(1).toString();
            connections.push_back(conn);
        }
    }
    
    return connections;
}

bool DatabaseManager::remove_folder_connection(const QString& session_folder, const QString& folder_path) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM tinder_folder_connections WHERE session_folder = ? AND folder_path = ?");
    query.addBindValue(session_folder);
    query.addBindValue(folder_path);
    return query.exec();
}

int DatabaseManager::get_next_connection_group_id(const QString& session_folder) {
    QSqlQuery query(db_);
    query.prepare("SELECT MAX(group_id) FROM tinder_folder_connections WHERE session_folder = ?");
    query.addBindValue(session_folder);
    
    if (query.exec() && query.next()) {
        return query.value(0).toInt() + 1;
    }
    return 1;
}

bool DatabaseManager::add_recent_folder(const QString& folder_path) {
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT OR REPLACE INTO recent_folders (folder_path, timestamp)
        VALUES (?, datetime('now'))
    )");
    query.addBindValue(folder_path);
    return query.exec();
}

bool DatabaseManager::remove_recent_folder(const QString& folder_path) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM recent_folders WHERE folder_path = ?");
    query.addBindValue(folder_path);
    return query.exec();
}

QStringList DatabaseManager::get_recent_folders(int limit) {
    QStringList folders;
    
    QSqlQuery query(db_);
    query.prepare("SELECT folder_path FROM recent_folders ORDER BY timestamp DESC LIMIT ?");
    query.addBindValue(limit);
    
    if (query.exec()) {
        while (query.next()) {
            folders.append(query.value(0).toString());
        }
    }
    
    return folders;
}

bool DatabaseManager::save_quick_access_folders(const QString& session_folder, const QStringList& folders) {
    // Create table if not exists
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS quick_access_folders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            folder_path TEXT NOT NULL,
            slot_order INTEGER NOT NULL,
            UNIQUE(session_folder, slot_order)
        )
    )");
    
    // Clear existing entries for this session
    QSqlQuery clear_query(db_);
    clear_query.prepare("DELETE FROM quick_access_folders WHERE session_folder = ?");
    clear_query.addBindValue(session_folder);
    clear_query.exec();
    
    // Insert new entries
    for (int i = 0; i < folders.size() && i < 10; ++i) {
        QSqlQuery insert_query(db_);
        insert_query.prepare(R"(
            INSERT INTO quick_access_folders (session_folder, folder_path, slot_order)
            VALUES (?, ?, ?)
        )");
        insert_query.addBindValue(session_folder);
        insert_query.addBindValue(folders[i]);
        insert_query.addBindValue(i);
        if (!insert_query.exec()) {
            return false;
        }
    }
    
    return true;
}

QStringList DatabaseManager::get_quick_access_folders(const QString& session_folder) {
    QStringList folders;
    
    // Ensure table exists
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS quick_access_folders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            folder_path TEXT NOT NULL,
            slot_order INTEGER NOT NULL,
            UNIQUE(session_folder, slot_order)
        )
    )");
    
    QSqlQuery query(db_);
    query.prepare("SELECT folder_path FROM quick_access_folders WHERE session_folder = ? ORDER BY slot_order");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            folders.append(query.value(0).toString());
        }
    }
    
    return folders;
}

bool DatabaseManager::save_source_folders(const QString& session_folder, const QStringList& folders) {
    // Clear existing entries for this session
    QSqlQuery clear_query(db_);
    clear_query.prepare("DELETE FROM session_source_folders WHERE session_folder = ?");
    clear_query.addBindValue(session_folder);
    clear_query.exec();
    
    // Insert new entries
    for (int i = 0; i < folders.size(); ++i) {
        QSqlQuery insert_query(db_);
        insert_query.prepare(R"(
            INSERT INTO session_source_folders (session_folder, source_path, sort_order)
            VALUES (?, ?, ?)
        )");
        insert_query.addBindValue(session_folder);
        insert_query.addBindValue(folders[i]);
        insert_query.addBindValue(i);
        if (!insert_query.exec()) {
            return false;
        }
    }
    
    return true;
}

QStringList DatabaseManager::get_source_folders(const QString& session_folder) {
    QStringList folders;
    
    QSqlQuery query(db_);
    query.prepare("SELECT source_path FROM session_source_folders WHERE session_folder = ? ORDER BY sort_order");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            folders.append(query.value(0).toString());
        }
    }
    
    return folders;
}

bool DatabaseManager::save_execution_log(const QString& session_folder, const QString& action,
                                         const QString& source_path, const QString& dest_path) {
    // Ensure table exists
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS execution_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            action TEXT NOT NULL,
            source_path TEXT NOT NULL,
            dest_path TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");
    
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT INTO execution_log (session_folder, action, source_path, dest_path)
        VALUES (?, ?, ?, ?)
    )");
    query.addBindValue(session_folder);
    query.addBindValue(action);
    query.addBindValue(source_path);
    query.addBindValue(dest_path);
    return query.exec();
}

std::vector<std::tuple<int, QString, QString, QString, QString>> DatabaseManager::get_execution_log(const QString& session_folder) {
    std::vector<std::tuple<int, QString, QString, QString, QString>> entries;
    
    // Ensure table exists
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS execution_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            action TEXT NOT NULL,
            source_path TEXT NOT NULL,
            dest_path TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");
    
    QSqlQuery query(db_);
    query.prepare(R"(
        SELECT id, action, source_path, dest_path, timestamp
        FROM execution_log
        WHERE session_folder = ?
        ORDER BY id DESC
    )");
    query.addBindValue(session_folder);
    
    if (query.exec()) {
        while (query.next()) {
            entries.emplace_back(
                query.value(0).toInt(),
                query.value(1).toString(),
                query.value(2).toString(),
                query.value(3).toString(),
                query.value(4).toString()
            );
        }
    }
    
    return entries;
}

bool DatabaseManager::remove_execution_log_entry(int id) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM execution_log WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::clear_execution_log(const QString& session_folder) {
    QSqlQuery query(db_);
    query.prepare("DELETE FROM execution_log WHERE session_folder = ?");
    query.addBindValue(session_folder);
    return query.exec();
}

std::vector<std::tuple<int, QString, QString, QString, QString>> DatabaseManager::get_all_execution_logs() {
    std::vector<std::tuple<int, QString, QString, QString, QString>> entries;

    execute_query(R"(
        CREATE TABLE IF NOT EXISTS execution_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            action TEXT NOT NULL,
            source_path TEXT NOT NULL,
            dest_path TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");

    QSqlQuery query(db_);
    query.prepare(R"(
        SELECT id, action, source_path, dest_path, timestamp
        FROM execution_log
        ORDER BY id DESC
    )");

    if (query.exec()) {
        while (query.next()) {
            entries.emplace_back(
                query.value(0).toInt(),
                query.value(1).toString(),
                query.value(2).toString(),
                query.value(3).toString(),
                query.value(4).toString()
            );
        }
    }

    return entries;
}

bool DatabaseManager::clear_all_execution_logs() {
    execute_query(R"(
        CREATE TABLE IF NOT EXISTS execution_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_folder TEXT NOT NULL,
            action TEXT NOT NULL,
            source_path TEXT NOT NULL,
            dest_path TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");

    QSqlQuery query(db_);
    query.prepare("DELETE FROM execution_log");
    return query.exec();
}

bool DatabaseManager::save_grid_config(const QString& session_folder, const QString& config_name,
                                       const QStringList& folder_paths) {
    // Delete existing config with this name
    QSqlQuery del(db_);
    del.prepare("DELETE FROM grid_configs WHERE session_folder = ? AND config_name = ?");
    del.addBindValue(session_folder);
    del.addBindValue(config_name);
    if (!del.exec()) return false;
    
    for (int i = 0; i < folder_paths.size(); ++i) {
        QSqlQuery ins(db_);
        ins.prepare("INSERT INTO grid_configs (session_folder, config_name, folder_path, sort_order) VALUES (?, ?, ?, ?)");
        ins.addBindValue(session_folder);
        ins.addBindValue(config_name);
        ins.addBindValue(folder_paths[i]);
        ins.addBindValue(i);
        if (!ins.exec()) return false;
    }
    return true;
}

QStringList DatabaseManager::get_grid_config(const QString& session_folder, const QString& config_name) {
    QStringList paths;
    
    QSqlQuery q(db_);
    q.prepare("SELECT folder_path FROM grid_configs WHERE session_folder = ? AND config_name = ? ORDER BY sort_order");
    q.addBindValue(session_folder);
    q.addBindValue(config_name);
    if (q.exec()) {
        while (q.next()) paths.append(q.value(0).toString());
    }
    return paths;
}

QStringList DatabaseManager::get_grid_config_names(const QString& session_folder) {
    QStringList names;
    
    QSqlQuery q(db_);
    q.prepare("SELECT DISTINCT config_name FROM grid_configs WHERE session_folder = ?");
    q.addBindValue(session_folder);
    if (q.exec()) {
        while (q.next()) names.append(q.value(0).toString());
    }
    return names;
}

bool DatabaseManager::delete_grid_config(const QString& session_folder, const QString& config_name) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM grid_configs WHERE session_folder = ? AND config_name = ?");
    q.addBindValue(session_folder);
    q.addBindValue(config_name);
    return q.exec();
}

bool DatabaseManager::save_ai_provider(const QString& provider_name, const QString& api_key,
                                       const QString& endpoint_url, const QString& model_name,
                                       bool is_local, int rate_limit_rpm) {
    QSqlQuery q(db_);
    q.prepare(R"(
        INSERT OR REPLACE INTO ai_providers 
        (provider_name, api_key, endpoint_url, model_name, is_local, rate_limit_rpm, last_used)
        VALUES (?, ?, ?, ?, ?, ?, datetime('now'))
    )");
    q.addBindValue(provider_name);
    q.addBindValue(api_key);
    q.addBindValue(endpoint_url);
    q.addBindValue(model_name);
    q.addBindValue(is_local ? 1 : 0);
    q.addBindValue(rate_limit_rpm);
    if (!q.exec()) {
        qWarning() << "Failed to save AI provider:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::get_ai_provider(const QString& provider_name, QString& api_key,
                                      QString& endpoint_url, QString& model_name,
                                      bool& is_local, int& rate_limit_rpm) {
    QSqlQuery q(db_);
    q.prepare("SELECT api_key, endpoint_url, model_name, is_local, rate_limit_rpm FROM ai_providers WHERE provider_name = ?");
    q.addBindValue(provider_name);
    if (q.exec() && q.next()) {
        api_key = q.value(0).toString();
        endpoint_url = q.value(1).toString();
        model_name = q.value(2).toString();
        is_local = q.value(3).toBool();
        rate_limit_rpm = q.value(4).toInt();
        return true;
    }
    return false;
}

QStringList DatabaseManager::get_ai_provider_names() {
    QStringList names;
    QSqlQuery q(db_);
    q.prepare("SELECT provider_name FROM ai_providers ORDER BY last_used DESC");
    if (q.exec()) {
        while (q.next()) names.append(q.value(0).toString());
    }
    return names;
}

int DatabaseManager::cleanup_stale_sessions(int days_old) {
    int cleaned = 0;
    
    QSqlQuery stale_query(db_);
    // Find sessions with all decisions older than N days
    QString modifier = QString("-%1 days").arg(days_old);
    stale_query.prepare(R"(
        SELECT DISTINCT folder_path FROM file_tinder_state 
        WHERE folder_path NOT IN (
            SELECT DISTINCT folder_path FROM file_tinder_state 
            WHERE timestamp > datetime('now', ?)
        )
    )");
    stale_query.addBindValue(modifier);
    
    QStringList stale_folders;
    if (stale_query.exec()) {
        while (stale_query.next()) {
            stale_folders.append(stale_query.value(0).toString());
        }
    }
    
    for (const QString& folder : stale_folders) {
        clear_session(folder);
        cleaned++;
    }
    
    return cleaned;
}
