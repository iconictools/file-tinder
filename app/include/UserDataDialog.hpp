#ifndef USER_DATA_DIALOG_HPP
#define USER_DATA_DIALOG_HPP

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class DatabaseManager;

class UserDataDialog : public QDialog {
    Q_OBJECT

public:
    explicit UserDataDialog(DatabaseManager& db, QWidget* parent = nullptr);
    ~UserDataDialog() override;

private slots:
    void load_activity_log();
    void clear_log();
    void export_csv();
    void generate_analysis();

private:
    void setup_ui();
    QWidget* create_activity_log_tab();
    QWidget* create_ai_analysis_tab();

    // AI helpers
    QString build_analysis_prompt();
    void send_analysis_request(const QString& prompt);
    void parse_and_display_response(const QByteArray& data);

    DatabaseManager& db_;

    // Activity Log tab
    QTableWidget* log_table_ = nullptr;
    QPushButton* clear_log_btn_ = nullptr;
    QPushButton* export_csv_btn_ = nullptr;

    // AI Analysis tab
    QTextEdit* analysis_display_ = nullptr;
    QPushButton* generate_btn_ = nullptr;
    QLabel* progress_label_ = nullptr;

    QNetworkAccessManager* network_manager_ = nullptr;
};

#endif // USER_DATA_DIALOG_HPP
