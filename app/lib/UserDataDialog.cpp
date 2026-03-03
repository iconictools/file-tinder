#include "UserDataDialog.hpp"
#include "DatabaseManager.hpp"
#include "AppLogger.hpp"
#include "ui_constants.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QApplication>

static const int kAnalysisTimeoutMs = 90000;

UserDataDialog::UserDataDialog(DatabaseManager& db, QWidget* parent)
    : QDialog(parent)
    , db_(db)
    , network_manager_(new QNetworkAccessManager(this)) {
    setup_ui();
    load_activity_log();
}

UserDataDialog::~UserDataDialog() = default;

void UserDataDialog::setup_ui() {
    setWindowTitle("User Data");
    setMinimumSize(ui::scaling::scaled(700), ui::scaling::scaled(500));

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    tabs->addTab(create_activity_log_tab(), "Activity Log");
    tabs->addTab(create_ai_analysis_tab(), "AI Analysis");

    layout->addWidget(tabs);
}

QWidget* UserDataDialog::create_activity_log_tab() {
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    log_table_ = new QTableWidget(tab);
    log_table_->setColumnCount(4);
    log_table_->setHorizontalHeaderLabels({"Date", "Action", "Source", "Destination"});
    log_table_->horizontalHeader()->setStretchLastSection(true);
    log_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    log_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    log_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    log_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    log_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    log_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    log_table_->setAlternatingRowColors(true);
    log_table_->verticalHeader()->setVisible(false);
    layout->addWidget(log_table_);

    auto* btn_row = new QHBoxLayout();
    btn_row->addStretch();

    export_csv_btn_ = new QPushButton("Export CSV", tab);
    export_csv_btn_->setStyleSheet(
        "QPushButton { padding: 6px 12px; background-color: #4a4a4a; color: #cccccc; border: 1px solid #555555; }"
        "QPushButton:hover { background-color: #555555; }"
    );
    connect(export_csv_btn_, &QPushButton::clicked, this, &UserDataDialog::export_csv);
    btn_row->addWidget(export_csv_btn_);

    clear_log_btn_ = new QPushButton("Clear Log", tab);
    clear_log_btn_->setStyleSheet(
        "QPushButton { padding: 6px 12px; background-color: #5a3030; color: #cccccc; border: 1px solid #704040; }"
        "QPushButton:hover { background-color: #704040; }"
    );
    connect(clear_log_btn_, &QPushButton::clicked, this, &UserDataDialog::clear_log);
    btn_row->addWidget(clear_log_btn_);

    layout->addLayout(btn_row);
    return tab;
}

QWidget* UserDataDialog::create_ai_analysis_tab() {
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* top_row = new QHBoxLayout();
    generate_btn_ = new QPushButton("Generate Analysis", tab);
    generate_btn_->setStyleSheet(
        "QPushButton { padding: 8px 16px; background-color: #2a5a3a; color: #cccccc; border: 1px solid #3a7a4a; font-weight: bold; }"
        "QPushButton:hover { background-color: #3a7a4a; }"
        "QPushButton:disabled { background-color: #3a3a3a; color: #666666; }"
    );
    connect(generate_btn_, &QPushButton::clicked, this, &UserDataDialog::generate_analysis);
    top_row->addWidget(generate_btn_);

    progress_label_ = new QLabel("", tab);
    progress_label_->setStyleSheet("color: #88aacc; font-style: italic;");
    top_row->addWidget(progress_label_);
    top_row->addStretch();

    layout->addLayout(top_row);

    analysis_display_ = new QTextEdit(tab);
    analysis_display_->setReadOnly(true);
    analysis_display_->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #3a3a3a; "
        "font-family: monospace; font-size: 12px; padding: 8px; }"
    );
    analysis_display_->setPlaceholderText(
        "Click \"Generate Analysis\" to analyze your file organization patterns using AI.");
    layout->addWidget(analysis_display_);

    return tab;
}

void UserDataDialog::load_activity_log() {
    auto entries = db_.get_all_execution_logs();
    log_table_->setRowCount(0);

    for (const auto& entry : entries) {
        int row = log_table_->rowCount();
        log_table_->insertRow(row);

        const auto& [id, action, source, dest, timestamp] = entry;

        log_table_->setItem(row, 0, new QTableWidgetItem(timestamp));
        log_table_->setItem(row, 1, new QTableWidgetItem(action));
        log_table_->setItem(row, 2, new QTableWidgetItem(source));
        log_table_->setItem(row, 3, new QTableWidgetItem(dest));
    }

    LOG_INFO("UserData", QString("Loaded %1 activity log entries").arg(entries.size()));
}

void UserDataDialog::clear_log() {
    auto reply = QMessageBox::question(this, "Clear Activity Log",
        "This will permanently delete all activity log entries.\n\nAre you sure?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        db_.clear_all_execution_logs();
        load_activity_log();
        LOG_INFO("UserData", "Activity log cleared");
    }
}

void UserDataDialog::export_csv() {
    QString path = QFileDialog::getSaveFileName(this, "Export Activity Log",
        "file_tinder_activity_log.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed", "Could not open file for writing.");
        return;
    }

    QTextStream out(&file);
    out << "Date,Action,Source,Destination\n";

    for (int row = 0; row < log_table_->rowCount(); ++row) {
        auto escape_csv = [](const QString& s) -> QString {
            if (s.contains(',') || s.contains('"') || s.contains('\n')) {
                QString escaped = s;
                escaped.replace("\"", "\"\"");
                return "\"" + escaped + "\"";
            }
            return s;
        };

        out << escape_csv(log_table_->item(row, 0)->text()) << ","
            << escape_csv(log_table_->item(row, 1)->text()) << ","
            << escape_csv(log_table_->item(row, 2)->text()) << ","
            << escape_csv(log_table_->item(row, 3)->text()) << "\n";
    }

    file.close();
    QMessageBox::information(this, "Export Complete",
        QString("Activity log exported to:\n%1").arg(path));
    LOG_INFO("UserData", QString("Exported %1 log entries to CSV").arg(log_table_->rowCount()));
}

void UserDataDialog::generate_analysis() {
    // Check if any AI provider is configured
    QStringList providers = db_.get_ai_provider_names();
    if (providers.isEmpty()) {
        analysis_display_->setPlainText(
            "Configure an AI provider in AI Mode to enable analysis.");
        return;
    }

    // Check if there is any log data
    auto entries = db_.get_all_execution_logs();
    if (entries.empty()) {
        analysis_display_->setPlainText("No activity log entries found. Use the app first to generate data.");
        return;
    }

    QString prompt = build_analysis_prompt();
    generate_btn_->setEnabled(false);
    progress_label_->setText("Analyzing...");
    analysis_display_->clear();
    QApplication::processEvents();

    send_analysis_request(prompt);
}

QString UserDataDialog::build_analysis_prompt() {
    auto entries = db_.get_all_execution_logs();

    QString history;
    int count = 0;
    for (const auto& entry : entries) {
        const auto& [id, action, source, dest, timestamp] = entry;
        history += QString("[%1] %2: %3").arg(timestamp, action, source);
        if (!dest.isEmpty()) {
            history += QString(" -> %1").arg(dest);
        }
        history += "\n";
        ++count;
        // Limit to 500 entries to avoid exceeding token limits
        if (count >= 500 && entries.size() > 500) {
            history += QString("... and %1 more entries\n").arg(static_cast<int>(entries.size()) - 500);
            break;
        }
    }

    return QString(
        "You are analyzing a user's file organization history from a file management application. "
        "Based on their actions below, provide a detailed analysis covering:\n\n"
        "1. ORGANIZATION PATTERNS: How does this user typically organize their files? "
        "What categories or folder structures do they prefer?\n\n"
        "2. USER PROFILE: What kind of person are they based on their digital habits? "
        "(creative, methodical, minimalist, hoarder, etc.)\n\n"
        "3. SYSTEM ANALYSIS: What was their folder structure like before, and what are they "
        "trying to build or achieve with their file management?\n\n"
        "4. RECOMMENDATIONS: Specific, actionable tips for this user to improve their "
        "file organization based on the patterns you observe.\n\n"
        "5. PERSONALITY INSIGHTS: What their file habits reveal about their personality "
        "and work style.\n\n"
        "User's file organization history (%1 entries):\n%2"
    ).arg(count).arg(history);
}

void UserDataDialog::send_analysis_request(const QString& prompt) {
    // Load the first available AI provider config
    QStringList providers = db_.get_ai_provider_names();
    if (providers.isEmpty()) {
        progress_label_->setText("");
        generate_btn_->setEnabled(true);
        analysis_display_->setPlainText("Configure an AI provider in AI Mode to enable analysis.");
        return;
    }

    QString provider_name = providers.first();
    QString api_key, endpoint_url, model_name;
    bool is_local = false;
    int rate_limit_rpm = 60;
    if (!db_.get_ai_provider(provider_name, api_key, endpoint_url, model_name, is_local, rate_limit_rpm)) {
        progress_label_->setText("");
        generate_btn_->setEnabled(true);
        analysis_display_->setPlainText("Failed to load AI provider configuration.");
        return;
    }

    QUrl url(endpoint_url);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(is_local ? 120000 : kAnalysisTimeoutMs);

    // Auth headers
    if (!api_key.isEmpty()) {
        if (provider_name == "Anthropic") {
            request.setRawHeader("x-api-key", api_key.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (provider_name != "Google Gemini") {
            request.setRawHeader("Authorization", QByteArray("Bearer ") + api_key.toUtf8());
        }
    }

    // Build request body
    QJsonObject body;
    if (provider_name == "Anthropic") {
        body["model"] = model_name;
        body["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = prompt;
        messages.append(msg);
        body["messages"] = messages;
    } else if (provider_name == "Google Gemini") {
        QString gemini_url = QString("%1/%2:generateContent?key=%3")
            .arg(endpoint_url, model_name, api_key);
        url = QUrl(gemini_url);
        request.setUrl(url);
        request.setRawHeader("Authorization", QByteArray());

        QJsonObject contents;
        QJsonArray parts;
        QJsonObject part;
        part["text"] = prompt;
        parts.append(part);
        contents["parts"] = parts;
        QJsonArray contents_arr;
        contents_arr.append(contents);
        body["contents"] = contents_arr;
    } else {
        // OpenAI-compatible
        body["model"] = model_name;
        body["temperature"] = 0.7;
        body["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = QString(
            "You are a file organization analyst. Provide detailed, insightful analysis "
            "of the user's file management patterns. Be specific and reference actual "
            "patterns from the data. Use clear section headers.");
        messages.append(sys_msg);
        QJsonObject user_msg;
        user_msg["role"] = "user";
        user_msg["content"] = prompt;
        messages.append(user_msg);
        body["messages"] = messages;
    }

    QNetworkReply* reply = network_manager_->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider_name]() {
        reply->deleteLater();
        progress_label_->setText("");
        generate_btn_->setEnabled(true);

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || (status > 0 && (status < 200 || status >= 300))) {
            QString err = reply->errorString();
            QString resp_body = QString::fromUtf8(reply->readAll()).left(500);
            analysis_display_->setPlainText(
                QString("AI request failed: %1\n\n%2").arg(err, resp_body));
            LOG_ERROR("UserData", QString("AI analysis request failed: %1").arg(err));
            return;
        }

        QByteArray data = reply->readAll();
        parse_and_display_response(data);
    });
}

void UserDataDialog::parse_and_display_response(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QString content_text;

    // Try Anthropic format
    QJsonArray content_arr = doc.object()["content"].toArray();
    if (!content_arr.isEmpty()) {
        content_text = content_arr[0].toObject()["text"].toString();
    }

    // Try Gemini format
    if (content_text.isEmpty()) {
        QJsonArray candidates = doc.object()["candidates"].toArray();
        if (!candidates.isEmpty()) {
            QJsonObject content = candidates[0].toObject()["content"].toObject();
            QJsonArray parts = content["parts"].toArray();
            if (!parts.isEmpty()) {
                content_text = parts[0].toObject()["text"].toString();
            }
        }
    }

    // Try OpenAI-compatible format
    if (content_text.isEmpty()) {
        QJsonArray choices = doc.object()["choices"].toArray();
        if (!choices.isEmpty()) {
            content_text = choices[0].toObject()["message"].toObject()["content"].toString();
        }
    }

    if (content_text.isEmpty()) {
        analysis_display_->setPlainText("Received an empty response from the AI provider.");
        LOG_ERROR("UserData", "Empty AI response for analysis");
        return;
    }

    analysis_display_->setPlainText(content_text);
    LOG_INFO("UserData", "AI analysis generated successfully");
}
