#include "AiFileTinderDialog.hpp"
#include "FolderTreeModel.hpp"
#include "MindMapView.hpp"
#include "DatabaseManager.hpp"
#include "AppLogger.hpp"
#include "ui_constants.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QMessageBox>
#include <QProgressDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QTextBrowser>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QDateTime>
#include <QScrollBar>
#include <QMenu>
#include <QProgressBar>
#include <QListView>
#include <QScrollArea>
#include <QFrame>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QListWidget>

// Default batch size for API calls (used in cost estimation)
static const int kBatchSize = 50;
// Timeout for API requests (ms) — higher for local LLMs
static const int kCloudTimeoutMs = 60000;
static const int kLocalTimeoutMs = 120000;

// ============================================================
// AiSetupDialog
// ============================================================

AiSetupDialog::AiSetupDialog(const QStringList& existing_folders,
                             int file_count,
                             DatabaseManager& db,
                             const QString& session_folder,
                             QWidget* parent)
    : QDialog(parent)
    , db_(db)
    , session_folder_(session_folder)
    , existing_folders_(existing_folders)
    , file_count_(file_count)
    , fetch_nam_(new QNetworkAccessManager(this)) {
    setWindowTitle("AI Sorting Setup");
    setMinimumSize(ui::scaling::scaled(480), ui::scaling::scaled(420));
    build_ui();
    load_saved_provider();
}

void AiSetupDialog::build_ui() {
    auto* outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* scroll_content = new QWidget();
    auto* main_layout = new QVBoxLayout(scroll_content);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(14, 14, 14, 14);

    auto* header = new QLabel("AI Sorting Configuration");
    header->setStyleSheet("font-size: 16px; font-weight: bold; color: #3498db;");
    header->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(header);

    // ── Provider Group ──
    auto* prov_group = new QGroupBox("AI Provider");
    prov_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* prov_layout = new QVBoxLayout(prov_group);
    prov_layout->setSpacing(4);

    auto* prov_row = new QHBoxLayout();
    prov_row->addWidget(new QLabel("Provider:"));
    provider_combo_ = new QComboBox();
    provider_combo_->setEditable(true);
    provider_combo_->addItems({"OpenAI", "Anthropic", "Google Gemini", "Mistral",
                               "Groq", "OpenRouter", "Ollama (Local)", "LM Studio (Local)",
                               "Custom"});
    QStringList saved = db_.get_ai_provider_names();
    for (const QString& name : saved) {
        if (provider_combo_->findText(name) < 0)
            provider_combo_->addItem(name);
    }
    prov_row->addWidget(provider_combo_, 1);
    prov_layout->addLayout(prov_row);

    auto* key_row = new QHBoxLayout();
    key_row->addWidget(new QLabel("API Key:"));
    api_key_edit_ = new QLineEdit();
    api_key_edit_->setEchoMode(QLineEdit::Password);
    api_key_edit_->setPlaceholderText("sk-... (leave empty for local LLM)");
    key_row->addWidget(api_key_edit_, 1);
    prov_layout->addLayout(key_row);

    auto* key_warning = new QLabel("Note: API keys are stored locally in plaintext. "
        "Do not use on shared or untrusted machines.");
    key_warning->setStyleSheet("color: #e67e22; font-size: 9px;");
    key_warning->setWordWrap(true);
    prov_layout->addWidget(key_warning);

    auto* ep_row = new QHBoxLayout();
    ep_row->addWidget(new QLabel("Endpoint:"));
    endpoint_edit_ = new QLineEdit();
    endpoint_edit_->setPlaceholderText("https://api.openai.com/v1/chat/completions");
    ep_row->addWidget(endpoint_edit_, 1);
    prov_layout->addLayout(ep_row);

    connect(endpoint_edit_, &QLineEdit::editingFinished, this, [this]() {
        QString text = endpoint_edit_->text().trimmed();
        if (!text.isEmpty()) {
            QUrl url(text);
            if (!url.isValid() || url.scheme().isEmpty()) {
                endpoint_edit_->setStyleSheet("QLineEdit { border: 1px solid #e74c3c; }");
                endpoint_edit_->setToolTip("Invalid URL: must start with http:// or https://");
            } else {
                endpoint_edit_->setStyleSheet("");
                endpoint_edit_->setToolTip("");
            }
        }
    });

    auto* model_row = new QHBoxLayout();
    model_row->addWidget(new QLabel("Model:"));
    model_combo_ = new QComboBox();
    model_combo_->setEditable(true);
    model_combo_->setInsertPolicy(QComboBox::InsertAtBottom);
    model_combo_->setPlaceholderText("Select or type a model name");
    model_row->addWidget(model_combo_, 1);
    auto* fetch_btn = new QPushButton("Fetch");
    fetch_btn->setFixedWidth(ui::scaling::scaled(60));
    fetch_btn->setToolTip("Fetch available models from the provider API");
    fetch_btn->setStyleSheet("QPushButton { padding: 3px 8px; }");
    connect(fetch_btn, &QPushButton::clicked, this, [this]() {
        fetch_models(provider_combo_->currentText());
    });
    model_row->addWidget(fetch_btn);
    auto* test_btn = new QPushButton("Test");
    test_btn->setFixedWidth(ui::scaling::scaled(50));
    test_btn->setToolTip("Test the API connection with a simple request");
    test_btn->setStyleSheet("QPushButton { padding: 3px 8px; background-color: #27ae60; color: white; border-radius: 3px; }"
                           "QPushButton:hover { background-color: #2ecc71; }");
    connect(test_btn, &QPushButton::clicked, this, [this, test_btn]() {
        QString api_key = api_key_edit_->text().trimmed();
        QString endpoint = endpoint_edit_->text().trimmed();
        QString model = model_combo_->currentText().trimmed();
        QString provider = provider_combo_->currentText();
        bool is_local = provider.contains("Ollama") || provider.contains("LM Studio") || provider.contains("Local");

        if (!is_local && api_key.isEmpty()) {
            QMessageBox::warning(this, "Test Failed", "API key is required for cloud providers.");
            return;
        }
        if (endpoint.isEmpty()) {
            QMessageBox::warning(this, "Test Failed", "Endpoint URL is required.");
            return;
        }

        // Build a minimal test request
        QUrl url(endpoint);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(10000);
        if (!is_local && !api_key.isEmpty()) {
            if (provider == "Anthropic") {
                request.setRawHeader("x-api-key", api_key.toUtf8());
                request.setRawHeader("anthropic-version", "2023-06-01");
            } else if (provider != "Google Gemini") {
                request.setRawHeader("Authorization", QByteArray("Bearer ") + api_key.toUtf8());
            }
        }

        QJsonObject body;
        body["model"] = model;
        body["max_tokens"] = 10;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = "Say OK";
        messages.append(msg);
        body["messages"] = messages;

        test_btn->setEnabled(false);
        test_btn->setText("...");
        QNetworkReply* reply = fetch_nam_->post(request, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, test_btn]() {
            test_btn->setEnabled(true);
            test_btn->setText("Test");
            reply->deleteLater();
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError || (status >= 200 && status < 300)) {
                QMessageBox::information(this, "Connection OK", 
                    QString("API connection successful (HTTP %1).").arg(status));
            } else {
                QMessageBox::warning(this, "Connection Failed",
                    QString("API returned HTTP %1.\n\n%2").arg(status).arg(reply->errorString()));
            }
        });
    });
    model_row->addWidget(test_btn);
    prov_layout->addLayout(model_row);

    auto* model_hint = new QLabel("Recommended: use the cheapest model (gpt-4o-mini, claude-3-haiku, gemini-1.5-flash, llama-3.1-8b) for file sorting.");
    model_hint->setStyleSheet("color: #7f8c8d; font-size: 9px;");
    model_hint->setWordWrap(true);
    prov_layout->addWidget(model_hint);

    // Recalculate cost when model name changes (free models suppress warning)
    connect(model_combo_, &QComboBox::currentTextChanged, this, [this]() { update_cost_estimate(); });

    // Provider preset auto-fill
    connect(provider_combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        QString api_key, endpoint, model;
        bool is_local = false;
        int rpm = 60;
        if (db_.get_ai_provider(text, api_key, endpoint, model, is_local, rpm)) {
            api_key_edit_->setText(api_key);
            endpoint_edit_->setText(endpoint);
            model_combo_->setCurrentText(model);
            update_cost_estimate();
            return;
        }
        model_combo_->clear();
        if (text == "OpenAI") {
            endpoint_edit_->setText("https://api.openai.com/v1/chat/completions");
            model_combo_->addItems({"gpt-4o-mini", "gpt-4o", "gpt-4-turbo", "gpt-3.5-turbo"});
            model_combo_->setCurrentText("gpt-4o-mini");
        } else if (text == "Anthropic") {
            endpoint_edit_->setText("https://api.anthropic.com/v1/messages");
            model_combo_->addItems({"claude-3-haiku-20240307", "claude-3-sonnet-20240229", "claude-3-opus-20240229"});
            model_combo_->setCurrentText("claude-3-haiku-20240307");
        } else if (text == "Google Gemini") {
            endpoint_edit_->setText("https://generativelanguage.googleapis.com/v1beta/models");
            model_combo_->addItems({"gemini-1.5-flash", "gemini-1.5-pro", "gemini-1.0-pro"});
            model_combo_->setCurrentText("gemini-1.5-flash");
        } else if (text == "Mistral") {
            endpoint_edit_->setText("https://api.mistral.ai/v1/chat/completions");
            model_combo_->addItems({"mistral-small-latest", "mistral-medium-latest", "mistral-large-latest"});
            model_combo_->setCurrentText("mistral-small-latest");
        } else if (text == "Groq") {
            endpoint_edit_->setText("https://api.groq.com/openai/v1/chat/completions");
            model_combo_->addItems({"llama-3.1-8b-instant", "llama-3.1-70b-versatile", "mixtral-8x7b-32768"});
            model_combo_->setCurrentText("llama-3.1-8b-instant");
        } else if (text == "OpenRouter") {
            endpoint_edit_->setText("https://openrouter.ai/api/v1/chat/completions");
            model_combo_->addItems({"meta-llama/llama-3.1-8b-instruct:free", "google/gemma-2-9b-it:free", "mistralai/mistral-7b-instruct:free"});
            model_combo_->setCurrentText("meta-llama/llama-3.1-8b-instruct:free");
        } else if (text.contains("Ollama")) {
            endpoint_edit_->setText("http://localhost:11434/v1/chat/completions");
            model_combo_->addItems({"llama3.1", "llama3", "mistral", "gemma2"});
            model_combo_->setCurrentText("llama3.1");
            api_key_edit_->clear();
        } else if (text.contains("LM Studio")) {
            endpoint_edit_->setText("http://localhost:1234/v1/chat/completions");
            model_combo_->addItem("local-model");
            model_combo_->setCurrentText("local-model");
            api_key_edit_->clear();
        }
        update_cost_estimate();
    });

    main_layout->addWidget(prov_group);

    // ── Mode Group ──
    auto* mode_group = new QGroupBox("Sorting Mode");
    mode_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* mode_layout = new QVBoxLayout(mode_group);
    mode_layout->setSpacing(4);

    auto_radio_ = new QRadioButton("Auto -- AI sorts all files, then review");
    auto_radio_->setChecked(true);
    auto_radio_->setToolTip("The AI assigns every file to a folder. You review and adjust on the review screen.");
    mode_layout->addWidget(auto_radio_);

    auto* semi_row = new QHBoxLayout();
    semi_radio_ = new QRadioButton("Semi -- AI suggests");
    semi_radio_->setToolTip("The AI highlights the top N matching folders per file in the grid.");
    semi_row->addWidget(semi_radio_);
    semi_count_spin_ = new QSpinBox();
    semi_count_spin_->setRange(2, 5);
    semi_count_spin_->setValue(3);
    semi_count_spin_->setEnabled(false);
    semi_row->addWidget(semi_count_spin_);
    semi_row->addWidget(new QLabel("folders per file"));
    semi_row->addStretch();
    mode_layout->addLayout(semi_row);

    connect(semi_radio_, &QRadioButton::toggled, semi_count_spin_, &QSpinBox::setEnabled);

    main_layout->addWidget(mode_group);

    // ── Categories + Depth (combined row) ──
    auto* cat_group = new QGroupBox("Category Handling");
    cat_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* cat_layout = new QVBoxLayout(cat_group);
    cat_layout->setSpacing(4);

    category_combo_ = new QComboBox();
    category_combo_->addItem("Keep existing categories", static_cast<int>(AiCategoryMode::KeepExisting));
    category_combo_->addItem("Generate new categories", static_cast<int>(AiCategoryMode::GenerateNew));
    category_combo_->addItem("Synthesize new categories (existing + AI)", static_cast<int>(AiCategoryMode::SynthesizeNew));
    category_combo_->addItem("Keep + Generate new categories", static_cast<int>(AiCategoryMode::KeepPlusGenerate));
    cat_layout->addWidget(category_combo_);

    auto* depth_row = new QHBoxLayout();
    depth_row->addWidget(new QLabel("Subcategory depth:"));
    depth_spin_ = new QSpinBox();
    depth_spin_->setRange(1, 3);
    depth_spin_->setValue(2);
    depth_spin_->setToolTip("1 = flat (Images/)\n2 = one sub-level (Images/Vacation/)\n3 = two sub-levels (Images/Vacation/Beach/)");
    depth_row->addWidget(depth_spin_);
    depth_row->addWidget(new QLabel("levels"));
    depth_row->addStretch();
    cat_layout->addLayout(depth_row);

    main_layout->addWidget(cat_group);

    // ── Purpose (optional) ──
    auto* purpose_group = new QGroupBox("What is this folder for? (optional)");
    purpose_group->setStyleSheet("QGroupBox { font-weight: bold; }");
    auto* purpose_layout = new QVBoxLayout(purpose_group);

    purpose_edit_ = new QTextEdit();
    purpose_edit_->setMaximumHeight(70);
    purpose_edit_->setPlaceholderText("e.g. 'This is my Downloads folder, organize by project and file type'");
    purpose_edit_->setStyleSheet("QTextEdit { background: #2d2d2d; color: #ecf0f1; border: 1px solid #4a6078; }");
    purpose_layout->addWidget(purpose_edit_);

    main_layout->addWidget(purpose_group);

    auto* budget_row = new QHBoxLayout();
    budget_row->addWidget(new QLabel("Max budget:"));
    auto* budget_spin = new QDoubleSpinBox();
    budget_spin->setRange(0.0, 100.0);
    budget_spin->setValue(0.0);
    budget_spin->setPrefix("$");
    budget_spin->setDecimals(2);
    budget_spin->setSingleStep(0.10);
    budget_spin->setToolTip("Set to $0.00 for no limit. Analysis will stop if estimated cost exceeds this.");
    budget_row->addWidget(budget_spin);
    budget_row->addStretch();
    main_layout->addLayout(budget_row);

    // ── Cost estimate ──
    cost_label_ = new QLabel("");
    cost_label_->setStyleSheet("color: #f39c12; font-size: 11px;");
    cost_label_->setWordWrap(true);
    main_layout->addWidget(cost_label_);
    auto* price_note = new QLabel("(Token prices are estimates and may change. Check your provider dashboard for exact costs.)");
    price_note->setStyleSheet("color: #7f8c8d; font-size: 8px;");
    price_note->setWordWrap(true);
    main_layout->addWidget(price_note);
    update_cost_estimate();

    main_layout->addStretch();

    // ── Buttons ──
    auto* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    auto* cancel_btn = new QPushButton("Cancel");
    cancel_btn->setStyleSheet("QPushButton { padding: 8px 20px; }");
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    btn_layout->addWidget(cancel_btn);

    auto* ok_btn = new QPushButton("Start Sorting");
    ok_btn->setStyleSheet(
        "QPushButton { padding: 8px 25px; background-color: #3498db; "
        "color: white; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2980b9; }"
    );
    connect(ok_btn, &QPushButton::clicked, this, [this]() {
        QString provider = provider_combo_->currentText();
        bool is_local = provider.contains("Ollama") || provider.contains("LM Studio")
                        || provider.contains("Local");
        if (!is_local && api_key_edit_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "API Key Required",
                "Please enter an API key for the selected provider.\n"
                "For local LLMs (Ollama, LM Studio), the key can be left empty.");
            return;
        }
        if (endpoint_edit_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Endpoint Required",
                "Please enter the API endpoint URL.");
            return;
        }
        save_provider_config();
        accept();
    });
    btn_layout->addWidget(ok_btn);

    scroll->setWidget(scroll_content);
    outer_layout->addWidget(scroll, 1);
    outer_layout->addLayout(btn_layout);
}

void AiSetupDialog::load_saved_provider() {
    QStringList names = db_.get_ai_provider_names();
    if (!names.isEmpty()) {
        QString name = names.first();
        int idx = provider_combo_->findText(name);
        if (idx >= 0) {
            provider_combo_->setCurrentIndex(idx);
        } else {
            provider_combo_->setCurrentText(name);
        }
    }
}

void AiSetupDialog::save_provider_config() {
    QString provider = provider_combo_->currentText();
    bool is_local = provider.contains("Ollama") || provider.contains("LM Studio")
                    || provider.contains("Local");
    db_.save_ai_provider(provider, api_key_edit_->text().trimmed(),
                         endpoint_edit_->text().trimmed(),
                         model_combo_->currentText().trimmed(),
                         is_local, default_rate_limit(provider));
}

void AiSetupDialog::update_cost_estimate() {
    QString provider = provider_combo_->currentText();
    QString model = model_combo_->currentText();
    bool is_local = provider.contains("Ollama") || provider.contains("LM Studio")
                    || provider.contains("Local");
    bool is_free = provider.contains("free", Qt::CaseInsensitive)
                   || model.contains("free", Qt::CaseInsensitive)
                   || model.contains(":free");
    if (is_local || is_free) {
        cost_label_->setText("Free (local/free-tier model)");
        cost_label_->setStyleSheet("color: #2ecc71; font-size: 11px;");
        return;
    }
    
    // Model-specific pricing ($/1M tokens) [input, output]
    double input_price = 0.15, output_price = 0.60;  // Default: GPT-4o-mini tier
    if (model.contains("gpt-4o-mini")) { input_price = 0.15; output_price = 0.60; }
    else if (model.contains("gpt-4o")) { input_price = 2.50; output_price = 10.0; }
    else if (model.contains("gpt-4-turbo")) { input_price = 10.0; output_price = 30.0; }
    else if (model.contains("gpt-3.5")) { input_price = 0.50; output_price = 1.50; }
    else if (model.contains("claude-3-haiku")) { input_price = 0.25; output_price = 1.25; }
    else if (model.contains("claude-3-sonnet") || model.contains("claude-3.5-sonnet")) { input_price = 3.0; output_price = 15.0; }
    else if (model.contains("claude-3-opus")) { input_price = 15.0; output_price = 75.0; }
    else if (model.contains("gemini-1.5-flash")) { input_price = 0.075; output_price = 0.30; }
    else if (model.contains("gemini-1.5-pro")) { input_price = 1.25; output_price = 5.0; }
    else if (model.contains("mistral-small")) { input_price = 0.20; output_price = 0.60; }
    else if (model.contains("mistral-large")) { input_price = 2.0; output_price = 6.0; }

    int batches = (file_count_ + kBatchSize - 1) / kBatchSize;
    double input_tokens = file_count_ * 200.0 + batches * 500.0;
    double output_tokens = file_count_ * 80.0;
    double cost = (input_tokens * input_price + output_tokens * output_price) / 1000000.0;
    
    cost_label_->setStyleSheet("color: #f39c12; font-size: 11px;");
    if (cost < 0.01) {
        cost_label_->setText(QString("Estimated cost: < $0.01 for %1 files (%2)")
            .arg(file_count_).arg(model));
    } else {
        cost_label_->setText(QString("Estimated cost: ~$%1 for %2 files (%3)")
            .arg(cost, 0, 'f', 3).arg(file_count_).arg(model));
    }
}

int AiSetupDialog::default_rate_limit(const QString& provider) const {
    if (provider.contains("Ollama") || provider.contains("LM Studio") || provider.contains("Local"))
        return 10000;
    if (provider == "OpenAI") return 500;
    if (provider == "Anthropic") return 50;
    if (provider == "Google Gemini") return 60;
    if (provider == "Mistral") return 60;
    if (provider == "Groq") return 30;
    if (provider == "OpenRouter") return 20;
    return 60;
}

void AiSetupDialog::fetch_models(const QString& provider) {
    QString api_key = api_key_edit_->text().trimmed();
    QString endpoint = endpoint_edit_->text().trimmed();
    bool is_local = provider.contains("Ollama") || provider.contains("LM Studio")
                    || provider.contains("Local");

    // Determine the models list endpoint
    QString models_url;
    if (provider.contains("Ollama")) {
        models_url = "http://localhost:11434/api/tags";
    } else if (provider.contains("LM Studio")) {
        models_url = "http://localhost:1234/v1/models";
    } else if (provider == "OpenAI" || provider == "Groq" || provider == "Mistral") {
        // OpenAI-compatible: /v1/models
        QUrl base(endpoint);
        QString path = base.path();
        int chat_pos = path.indexOf("/chat/completions");
        if (chat_pos >= 0) path = path.left(chat_pos) + "/models";
        else path = "/v1/models";
        base.setPath(path);
        models_url = base.toString();
    } else if (provider == "OpenRouter") {
        models_url = "https://openrouter.ai/api/v1/models";
    } else if (provider == "Anthropic") {
        models_url = "https://api.anthropic.com/v1/models";
    } else if (provider == "Google Gemini") {
        if (api_key.isEmpty()) {
            QMessageBox::warning(this, "Fetch Models", "API key is required for Google Gemini model listing.");
            return;
        }
        models_url = QString("https://generativelanguage.googleapis.com/v1beta/models?key=%1").arg(api_key);
    } else {
        // Custom/unknown: try OpenAI-compatible
        QUrl base(endpoint);
        QString path = base.path();
        int chat_pos = path.indexOf("/chat/completions");
        if (chat_pos >= 0) path = path.left(chat_pos) + "/models";
        base.setPath(path);
        models_url = base.toString();
    }

    QUrl fetch_url(models_url);
    QNetworkRequest request(fetch_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(10000);
    if (!is_local && !api_key.isEmpty()) {
        if (provider == "Anthropic") {
            request.setRawHeader("x-api-key", api_key.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (provider != "Google Gemini") {
            request.setRawHeader("Authorization", QByteArray("Bearer ") + api_key.toUtf8());
        }
    }

    model_combo_->setEnabled(false);
    QString current_model = model_combo_->currentText();

    QNetworkReply* reply = fetch_nam_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, current_model, provider]() {
        model_combo_->setEnabled(true);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Fetch Failed",
                QString("Could not fetch models: %1").arg(reply->errorString()));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QStringList models;

        if (provider.contains("Ollama")) {
            // Ollama returns {"models": [{"name": "llama3:latest", ...}]}
            QJsonArray arr = doc.object()["models"].toArray();
            for (const QJsonValue& v : arr) {
                models.append(v.toObject()["name"].toString());
            }
        } else if (provider == "Google Gemini") {
            // Gemini returns {"models": [{"name": "models/gemini-1.5-flash", ...}]}
            QJsonArray arr = doc.object()["models"].toArray();
            for (const QJsonValue& v : arr) {
                QString name = v.toObject()["name"].toString();
                if (name.startsWith("models/")) name = name.mid(7);
                models.append(name);
            }
        } else {
            // OpenAI-compatible / Anthropic: {"data": [{"id": "...", ...}]}
            QJsonArray arr = doc.object()["data"].toArray();
            for (const QJsonValue& v : arr) {
                models.append(v.toObject()["id"].toString());
            }
        }

        if (models.isEmpty()) {
            QMessageBox::information(this, "No Models",
                "No models returned by the API. You can type a model name manually.");
            return;
        }

        models.sort();
        model_combo_->clear();
        model_combo_->addItems(models);

        // Restore previous selection if it's still in the list
        int idx = model_combo_->findText(current_model);
        if (idx >= 0) model_combo_->setCurrentIndex(idx);
        else model_combo_->setCurrentIndex(0);
    });
}

AiSortMode AiSetupDialog::sort_mode() const {
    return auto_radio_->isChecked() ? AiSortMode::Auto : AiSortMode::Semi;
}

int AiSetupDialog::semi_mode_count() const {
    return semi_count_spin_->value();
}

AiCategoryMode AiSetupDialog::category_mode() const {
    return static_cast<AiCategoryMode>(category_combo_->currentData().toInt());
}

int AiSetupDialog::category_depth() const {
    return depth_spin_->value();
}

QString AiSetupDialog::folder_purpose() const {
    return purpose_edit_->toPlainText().trimmed();
}

AiProviderConfig AiSetupDialog::provider_config() const {
    AiProviderConfig cfg;
    cfg.provider_name = provider_combo_->currentText();
    cfg.api_key = api_key_edit_->text().trimmed();
    cfg.endpoint_url = endpoint_edit_->text().trimmed();
    cfg.model_name = model_combo_->currentText().trimmed();
    cfg.is_local = cfg.provider_name.contains("Ollama") || cfg.provider_name.contains("LM Studio")
                   || cfg.provider_name.contains("Local");
    cfg.rate_limit_rpm = default_rate_limit(cfg.provider_name);
    return cfg;
}

int AiSetupDialog::confidence_threshold() const {
    return 0;
}

// ============================================================
// AiFileTinderDialog
// ============================================================

AiFileTinderDialog::AiFileTinderDialog(const QString& source_folder,
                                       DatabaseManager& db,
                                       QWidget* parent)
    : AdvancedFileTinderDialog(source_folder, db, parent)
    , sort_mode_(AiSortMode::Auto)
    , category_mode_(AiCategoryMode::KeepExisting)
    , semi_count_(3)
    , category_depth_(2)
    , network_manager_(new QNetworkAccessManager(this))
    , rate_limit_timer_(new QTimer(this))
    , requests_this_minute_(0) {

    setWindowTitle(QString("File Tinder - AI Mode \xe2\x80\x94 %1").arg(QFileInfo(source_folder).fileName()));

    rate_limit_timer_->setInterval(60000);
    connect(rate_limit_timer_, &QTimer::timeout, this, &AiFileTinderDialog::reset_rate_limit);
}

AiFileTinderDialog::~AiFileTinderDialog() = default;

void AiFileTinderDialog::initialize() {
    AdvancedFileTinderDialog::initialize();

    // Replace the "Basic Mode" switch button with a mode menu for AI mode
    if (switch_mode_btn_) {
        switch_mode_btn_->disconnect();
        switch_mode_btn_->setText("Switch Mode");
        connect(switch_mode_btn_, &QPushButton::clicked, this, [this]() {
            QMenu menu(this);
            auto* basic_action = menu.addAction("Basic Mode");
            auto* adv_action = menu.addAction("Advanced Mode");
            QAction* selected = menu.exec(switch_mode_btn_->mapToGlobal(
                QPoint(0, switch_mode_btn_->height())));
            if (selected == basic_action) {
                save_session_state();
                emit switch_to_basic_mode();
            } else if (selected == adv_action) {
                save_session_state();
                emit switch_to_advanced_mode();
            }
        });
    }

    // Add AI buttons to the title bar
    if (auto* main_layout = qobject_cast<QVBoxLayout*>(layout())) {
        // "AI Setup" button — opens the configuration dialog
        ai_setup_btn_ = new QPushButton("AI Setup");
        ai_setup_btn_->setStyleSheet(
            "QPushButton { padding: 5px 12px; background-color: #2980b9; "
            "border-radius: 4px; color: white; font-size: 11px; }"
            "QPushButton:hover { background-color: #3498db; }"
        );
        ai_setup_btn_->setToolTip("Configure AI provider, model, and sorting options");
        connect(ai_setup_btn_, &QPushButton::clicked, this, [this]() {
            if (show_ai_setup()) {
                ai_configured_ = true;
                run_ai_analysis(false);
            }
        });

        // "AI Actions" button
        rerun_ai_btn_ = new QPushButton("AI Actions");
        rerun_ai_btn_->setStyleSheet(
            "QPushButton { padding: 5px 12px; background-color: #3498db; "
            "border-radius: 4px; color: white; font-size: 11px; }"
            "QPushButton:hover { background-color: #2980b9; }"
        );
        rerun_ai_btn_->setToolTip("Configure AI provider in 'AI Setup' to enable re-running");
        rerun_ai_btn_->setEnabled(false);
        connect(rerun_ai_btn_, &QPushButton::clicked, this, [this]() {
            if (!ai_configured_) {
                QMessageBox::information(this, "AI Not Configured",
                    "Please click 'AI Setup' first to configure the AI provider.");
                return;
            }
            QMenu menu;
            auto* remaining_action = menu.addAction("Analyze remaining files");
            auto* all_action = menu.addAction("Re-analyze all files");
            menu.addSeparator();
            auto* recat_action = menu.addAction("Generate new categories");
            recat_action->setToolTip("Re-analyze already-sorted files that might better fit new/changed categories");

            auto* chosen = menu.exec(rerun_ai_btn_->mapToGlobal(QPoint(0, rerun_ai_btn_->height())));
            if (!chosen) return;

            if (chosen == remaining_action) {
                run_ai_analysis(true);
            } else if (chosen == all_action) {
                run_ai_analysis(false);
            } else if (chosen == recat_action) {
                // Re-categorize: re-analyze files that are already "move" decisions
                // to see if new/changed folders are a better fit
                int moved_count = 0;
                for (auto& f : files_) {
                    if (f.decision == "move") moved_count++;
                }
                if (moved_count == 0) {
                    QMessageBox::information(this, "No Sorted Files",
                        "No files are currently sorted. Use 'Remaining unsorted' instead.");
                    return;
                }
                auto reply = QMessageBox::question(this, "Re-categorize",
                    QString("Re-analyze %1 already-sorted files against the current folder structure?\n\n"
                            "Files may be moved to different/new folders if the AI finds a better fit.")
                        .arg(moved_count),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (reply == QMessageBox::Yes) {
                    // Reset moved files to pending so they get re-analyzed
                    for (auto& f : files_) {
                        if (f.decision == "move") {
                            if (folder_model_) folder_model_->unassign_file_from_folder(f.destination_folder);
                            f.decision = "pending";
                            f.destination_folder.clear();
                            move_count_--;
                        }
                    }
                    update_stats();
                    run_ai_analysis(true);
                }
            }
        });

        if (main_layout->count() > 0) {
            auto* first_item = main_layout->itemAt(0);
            if (first_item && first_item->widget()) {
                auto* title_bar = first_item->widget();
                auto* title_layout = qobject_cast<QHBoxLayout*>(title_bar->layout());
                if (title_layout) {
                    title_layout->insertWidget(title_layout->count() - 1, ai_setup_btn_);
                    title_layout->insertWidget(title_layout->count() - 1, rerun_ai_btn_);

                    ai_glow_toggle_ = new QCheckBox("Glow AI");
                    ai_glow_toggle_->setChecked(true);
                    ai_glow_toggle_->setToolTip("Highlight AI-suggested folders with a gold glow border in the grid");
                    ai_glow_toggle_->setStyleSheet("QCheckBox { color: #f1c40f; font-size: 10px; }");
                    connect(ai_glow_toggle_, &QCheckBox::toggled, this, [this](bool checked) {
                        if (!checked) {
                            if (mind_map_view_) mind_map_view_->clear_highlighted_paths();
                        } else {
                            // Re-apply highlights for current file
                            if (!highlighted_folders_.isEmpty() && mind_map_view_) {
                                mind_map_view_->set_highlighted_paths(highlighted_folders_);
                            }
                        }
                    });
                    title_layout->insertWidget(title_layout->count() - 1, ai_glow_toggle_);

                    ai_reasoning_toggle_ = new QCheckBox("AI Reasoning");
                    ai_reasoning_toggle_->setChecked(true);
                    ai_reasoning_toggle_->setToolTip("Show AI reasoning text for each file");
                    ai_reasoning_toggle_->setStyleSheet("QCheckBox { color: #95a5a6; font-size: 10px; }");
                    connect(ai_reasoning_toggle_, &QCheckBox::toggled, this, [this](bool checked) {
                        if (ai_reasoning_label_) {
                            if (!checked) {
                                ai_reasoning_label_->setVisible(false);
                            } else {
                                // Re-show reasoning for current file if available
                                int file_idx = get_current_file_index();
                                for (const auto& s : suggestions_) {
                                    if (s.file_index == file_idx && !s.reasoning.isEmpty()) {
                                        ai_reasoning_label_->setVisible(true);
                                        break;
                                    }
                                }
                            }
                        }
                    });
                    title_layout->insertWidget(title_layout->count() - 1, ai_reasoning_toggle_);
                }
            }
        }

        // Create separate "AI Suggestions" panel below the grid
        ai_suggestions_panel_ = new QWidget();
        ai_suggestions_panel_->setVisible(false);
        auto* ai_sugg_layout = new QHBoxLayout(ai_suggestions_panel_);
        ai_sugg_layout->setContentsMargins(0, 0, 0, 0);
        auto* ai_sugg_label = new QLabel("AI Suggestions:");
        ai_sugg_label->setStyleSheet("font-weight: bold; color: #3498db;");
        ai_sugg_layout->addWidget(ai_sugg_label);

        ai_suggestions_list_ = new QListWidget();
        ai_suggestions_list_->setFlow(QListView::LeftToRight);
        ai_suggestions_list_->setMaximumHeight(40);
        ai_suggestions_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ai_suggestions_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ai_suggestions_list_->setStyleSheet(
            "QListWidget { background: transparent; border: none; }"
            "QListWidget::item { padding: 4px 10px; background: #1a3a5c; border-radius: 3px; "
            "margin-right: 4px; color: #3498db; font-weight: bold; }"
            "QListWidget::item:hover { background: #1e4a6e; }"
            "QListWidget::item:selected { background: #2980b9; color: white; }"
        );
        connect(ai_suggestions_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            QString folder_path = item->data(Qt::UserRole).toString();
            if (!folder_path.isEmpty()) {
                on_folder_clicked_from_ai(folder_path);
            }
        });
        ai_sugg_layout->addWidget(ai_suggestions_list_, 1);

        // Insert after the mind map / before the quick access panel
        if (main_layout->count() > 2) {
            main_layout->insertWidget(main_layout->count() - 2, ai_suggestions_panel_);
        } else {
            main_layout->addWidget(ai_suggestions_panel_);
        }

        ai_reasoning_label_ = new QLabel();
        ai_reasoning_label_->setStyleSheet("color: #95a5a6; font-size: 10px; font-style: italic; padding: 2px 4px;");
        ai_reasoning_label_->setWordWrap(true);
        ai_reasoning_label_->setVisible(false);
        ai_sugg_layout->addWidget(ai_reasoning_label_);
    }
}

bool AiFileTinderDialog::show_ai_setup() {
    QStringList existing_folders;
    if (folder_model_) {
        existing_folders = folder_model_->get_all_folder_paths();
    }

    AiSetupDialog setup(existing_folders, static_cast<int>(files_.size()),
                        db_, source_folder_, this);
    if (setup.exec() != QDialog::Accepted) {
        return false;
    }

    sort_mode_ = setup.sort_mode();
    semi_count_ = setup.semi_mode_count();
    category_mode_ = setup.category_mode();
    category_depth_ = setup.category_depth();
    folder_purpose_ = setup.folder_purpose();
    provider_config_ = setup.provider_config();
    confidence_threshold_ = setup.confidence_threshold();

    // Validate: KeepExisting with empty grid is incompatible
    QStringList existing = folder_model_ ? folder_model_->get_all_folder_paths() : QStringList();
    if (category_mode_ == AiCategoryMode::KeepExisting && existing.isEmpty()) {
        QMessageBox::warning(this, "No Folders",
            "\"Keep existing categories\" requires at least one folder in the grid.\n"
            "Switching to \"Generate new categories\" automatically.");
        category_mode_ = AiCategoryMode::GenerateNew;
    }
    // SynthesizeNew with empty grid falls back to GenerateNew
    if (category_mode_ == AiCategoryMode::SynthesizeNew && existing.isEmpty()) {
        category_mode_ = AiCategoryMode::GenerateNew;
    }

    // Detect free-tier usage for smart rate handling
    is_free_tier_ = provider_config_.model_name.contains("free", Qt::CaseInsensitive)
                    || provider_config_.model_name.contains(":free")
                    || provider_config_.provider_name == "Groq";
    consecutive_429s_ = 0;

    rate_limit_timer_->start();
    return true;
}

int AiFileTinderDialog::calculate_batch_size(int folder_count, int total_files) const {
    // Base batch size
    int base = 50;

    // Reduce for large folder counts (more context per request)
    if (folder_count > 30) base = 30;
    else if (folder_count > 15) base = 40;

    // Local models can handle larger batches (no token cost)
    if (provider_config_.is_local) base = qMin(base + 20, 100);

    // For very small file sets, use a single batch
    if (total_files <= base) return total_files;

    // For free-tier APIs with strict rate limits, use smaller batches
    if (is_free_tier_ && provider_config_.rate_limit_rpm <= 30) {
        base = qMin(base, 25);
    }

    return base;
}

void AiFileTinderDialog::run_ai_analysis(bool remaining_only) {
    if (files_.empty()) {
        QMessageBox::information(this, "No Files", "No files to analyze.");
        return;
    }

    // When overwriting all decisions, reset existing ones first
    if (!remaining_only) {
        for (auto& file : files_) {
            if (file.decision == "move" && folder_model_) {
                folder_model_->unassign_file_from_folder(file.destination_folder);
            }
            if (file.decision == "keep") keep_count_--;
            else if (file.decision == "delete") delete_count_--;
            else if (file.decision == "sort_later") sort_later_count_--;
            else if (file.decision == "move") move_count_--;
            file.decision = "pending";
            file.destination_folder.clear();
        }
    }

    // Determine which files to analyze
    std::vector<int> file_indices;
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
        if (!remaining_only || files_[i].decision == "pending") {
            file_indices.push_back(i);
        }
    }
    if (file_indices.empty()) {
        QMessageBox::information(this, "No Files", "No unsorted files remaining.");
        return;
    }

    // Build file descriptions
    QStringList file_descriptions;
    for (int idx : file_indices) {
        const auto& f = files_[idx];
        file_descriptions.append(QString("%1|%2|%3|%4|%5")
            .arg(idx).arg(f.name).arg(f.extension).arg(f.size).arg(f.mime_type));
    }

    // Build available folders list
    QStringList available_folders;
    if (folder_model_) {
        available_folders = folder_model_->get_all_folder_paths();
    }
    if (!available_folders.contains(source_folder_)) {
        available_folders.prepend(source_folder_);
    }

    // Calculate batches with dynamic sizing
    int total_files = static_cast<int>(file_descriptions.size());
    int batch_size = calculate_batch_size(static_cast<int>(available_folders.size()), total_files);
    int total_batches = (total_files + batch_size - 1) / batch_size;

    // Progress dialog with real-time log
    QDialog progress_dialog(this);
    progress_dialog.setWindowTitle("AI Analysis");
    progress_dialog.setMinimumSize(ui::scaling::scaled(550), ui::scaling::scaled(400));
    auto* prog_layout = new QVBoxLayout(&progress_dialog);

    auto* prog_header = new QLabel(QString("Analyzing %1 files...").arg(total_files));
    prog_header->setStyleSheet("font-size: 14px; font-weight: bold; color: #3498db;");
    prog_layout->addWidget(prog_header);

    auto* prog_bar = new QProgressBar();
    prog_bar->setRange(0, total_batches);
    prog_bar->setValue(0);
    prog_bar->setFormat("Batch %v / %m");
    prog_layout->addWidget(prog_bar);

    auto* file_prog_bar = new QProgressBar();
    file_prog_bar->setRange(0, total_files);
    file_prog_bar->setValue(0);
    file_prog_bar->setFormat("Files classified: %v / %m (%p%)");
    file_prog_bar->setStyleSheet(
        "QProgressBar { border: 1px solid #3c3c3c; border-radius: 3px; text-align: center; background-color: #2d2d2d; }"
        "QProgressBar::chunk { background-color: #27ae60; }");
    prog_layout->addWidget(file_prog_bar);

    auto* log_browser = new QTextBrowser();
    log_browser->setStyleSheet("QTextBrowser { background: #1a1a2e; color: #e0e0e0; font-family: monospace; font-size: 11px; }");
    log_browser->setReadOnly(true);
    prog_layout->addWidget(log_browser, 1);

    auto* cancel_btn = new QPushButton("Cancel");
    cancel_btn->setStyleSheet("QPushButton { padding: 6px 16px; }");
    prog_layout->addWidget(cancel_btn, 0, Qt::AlignRight);

    bool cancelled = false;
    connect(cancel_btn, &QPushButton::clicked, this, [&cancelled]() { cancelled = true; });

    progress_dialog.show();
    QApplication::processEvents();

    auto log = [&](const QString& msg) {
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        log_browser->append(QString("[%1] %2").arg(ts, msg));
        log_browser->verticalScrollBar()->setValue(log_browser->verticalScrollBar()->maximum());
        QApplication::processEvents();
    };

    log(QString("Starting AI analysis of %1 files...").arg(total_files));
    log(QString("Provider: %1 (%2) | Rate: %3 req/min")
        .arg(provider_config_.provider_name, provider_config_.model_name)
        .arg(provider_config_.rate_limit_rpm));

    QString cat_mode_name;
    switch (category_mode_) {
        case AiCategoryMode::KeepExisting: cat_mode_name = "Keep existing"; break;
        case AiCategoryMode::GenerateNew: cat_mode_name = "Generate new"; break;
        case AiCategoryMode::SynthesizeNew: cat_mode_name = "Synthesize new"; break;
        case AiCategoryMode::KeepPlusGenerate: cat_mode_name = "Keep + Generate"; break;
    }
    log(QString("Category mode: %1 | Depth: %2").arg(cat_mode_name).arg(category_depth_));

    // Clear previous suggestions for files we're re-analyzing
    if (remaining_only) {
        // Keep suggestions for already-sorted files
        std::vector<AiFileSuggestion> kept;
        for (const auto& s : suggestions_) {
            if (s.file_index >= 0 && s.file_index < static_cast<int>(files_.size())
                && files_[s.file_index].decision != "pending") {
                kept.push_back(s);
            }
        }
        suggestions_ = kept;
    } else {
        suggestions_.clear();
    }

    QElapsedTimer elapsed;
    elapsed.start();
    int files_classified = 0;

    for (int batch = 0; batch < total_batches && !cancelled; ++batch) {
        int start = batch * batch_size;
        int end = qMin(start + batch_size, total_files);
        int current_batch_size = end - start;

        log(QString("Batch %1/%2 \xe2\x80\x94 analyzing files %3-%4...")
            .arg(batch + 1).arg(total_batches).arg(start + 1).arg(end));

        // Build batch file list
        QStringList batch_files;
        for (int i = start; i < end; ++i) {
            batch_files.append(file_descriptions[i]);
        }

        // Smart rate limiting: enforce per-request delay and per-minute cap
        if (!check_rate_limit()) {
            int wait_secs = 60 - (elapsed.elapsed() / 1000) % 60;
            if (wait_secs <= 0) wait_secs = 5;
            log(QString("Rate limit reached: waiting %1s...").arg(wait_secs));
            QEventLoop wait_loop;
            QTimer::singleShot(wait_secs * 1000, &wait_loop, &QEventLoop::quit);
            wait_loop.exec();
            reset_rate_limit();
        } else if (batch > 0) {
            // Smart pacing: add delay between requests for free/strict APIs
            int delay_ms = 0;
            if (provider_config_.is_local) {
                delay_ms = 0;
            } else if (is_free_tier_) {
                delay_ms = 3000 + consecutive_429s_ * 2000;  // 3s base + backoff
            } else if (provider_config_.rate_limit_rpm <= 30) {
                delay_ms = 2500;  // conservative for strict limits
            } else if (provider_config_.rate_limit_rpm <= 60) {
                delay_ms = 1200;
            } else {
                delay_ms = 500;   // light pacing for generous limits
            }
            if (delay_ms > 0) {
                log(QString("Pacing: %1ms delay...").arg(delay_ms));
                QEventLoop pace_loop;
                QTimer::singleShot(delay_ms, &pace_loop, &QEventLoop::quit);
                pace_loop.exec();
            }
        }

        // Build prompt for this batch
        QString prompt = build_analysis_prompt(batch_files, available_folders);

        // Send request
        QElapsedTimer batch_timer;
        batch_timer.start();

        QString error;
        QString response = send_api_request(prompt, error);

        if (!error.isEmpty()) {
            log(QString("ERROR: Batch %1/%2 failed: %3").arg(batch + 1).arg(total_batches).arg(error));

            // Error recovery: let user choose (with Retry option for API overloads)
            progress_dialog.hide();
            QMessageBox err_box(this);
            err_box.setWindowTitle("AI Analysis Interrupted");
            err_box.setText(QString("AI analysis interrupted after %1/%2 files.\n\n"
                        "%3 files remain unclassified.")
                    .arg(files_classified).arg(total_files).arg(total_files - files_classified));
            err_box.setInformativeText("Retry will re-attempt the failed batch. Continue will proceed with partial results. Abort will cancel the analysis.");
            auto* retry_btn = err_box.addButton("Retry", QMessageBox::AcceptRole);
            retry_btn->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 4px 12px; border-radius: 3px; }");
            err_box.addButton("Continue with partial results", QMessageBox::RejectRole);
            auto* abort_btn = err_box.addButton(QMessageBox::Abort);
            err_box.setDefaultButton(retry_btn);
            err_box.exec();

            if (err_box.clickedButton() == abort_btn) {
                suggestions_.clear();
                return;
            }
            if (err_box.clickedButton() == retry_btn) {
                batch--;  // Retry the same batch
                log("Retrying failed batch...");
                progress_dialog.show();
                QApplication::processEvents();
                continue;
            }
            // Continue to review with partial results
            break;
        }

        double batch_secs = batch_timer.elapsed() / 1000.0;

        // Parse response
        auto batch_suggestions = parse_ai_response(response);
        int parsed_count = static_cast<int>(batch_suggestions.size());
        files_classified += parsed_count;
        file_prog_bar->setValue(files_classified);

        // If partial parse failure, retry once
        if (parsed_count < current_batch_size) {
            int failed = current_batch_size - parsed_count;
            log(QString("%1/%2 files parsed. Retrying %3 failed...")
                .arg(parsed_count).arg(current_batch_size).arg(failed));

            // Build a retry prompt with just the failed file indices
            QSet<int> parsed_indices;
            for (const auto& s : batch_suggestions) parsed_indices.insert(s.file_index);

            QStringList retry_files;
            for (int i = start; i < end; ++i) {
                int orig_idx = file_descriptions[i].split('|').first().toInt();
                if (!parsed_indices.contains(orig_idx)) {
                    retry_files.append(file_descriptions[i]);
                }
            }

            if (!retry_files.isEmpty()) {
                QString retry_prompt = build_analysis_prompt(retry_files, available_folders);
                retry_prompt += "\nIMPORTANT: Return ONLY valid JSON. No extra text.\n";
                QString retry_error;
                QString retry_response = send_api_request(retry_prompt, retry_error);
                if (retry_error.isEmpty()) {
                    auto retry_results = parse_ai_response(retry_response);
                    for (auto& s : retry_results) {
                        batch_suggestions.push_back(s);
                    }
                    files_classified += static_cast<int>(retry_results.size());
                    file_prog_bar->setValue(files_classified);
                    log(QString("  Retry recovered %1 more files").arg(retry_results.size()));
                }
            }
        }

        // Store suggestions
        for (auto& s : batch_suggestions) {
            suggestions_.push_back(s);
        }

        log(QString("Batch %1/%2 complete \xe2\x80\x94 %3 files classified (%4s)")
            .arg(batch + 1).arg(total_batches).arg(files_classified)
            .arg(batch_secs, 0, 'f', 1));

        prog_bar->setValue(batch + 1);
        QApplication::processEvents();
    }

    double total_secs = elapsed.elapsed() / 1000.0;
    log(QString("Analysis complete -- %1 files classified (%2s)")
        .arg(files_classified).arg(total_secs, 0, 'f', 1));

    // Handle new categories: add AI-generated folders to the tree
    if (category_mode_ != AiCategoryMode::KeepExisting) {
        QSet<QString> new_folders;
        for (const auto& s : suggestions_) {
            for (const QString& folder : s.suggested_folders) {
                if (folder_model_ && !folder_model_->find_node(folder)
                    && folder != source_folder_) {
                    new_folders.insert(folder);
                }
            }
        }

        // Let user review proposed categories before applying
        if (!new_folders.isEmpty()) {
            QDialog review_dlg(this);
            review_dlg.setWindowTitle("Review AI Categories");
            review_dlg.setMinimumSize(ui::scaling::scaled(500), ui::scaling::scaled(400));
            auto* rv_layout = new QVBoxLayout(&review_dlg);

            auto* rv_header = new QLabel(QString("AI proposed %1 new folder(s). Edit, remove, or add categories:")
                .arg(new_folders.size()));
            rv_header->setStyleSheet("font-weight: bold; color: #3498db;");
            rv_header->setWordWrap(true);
            rv_layout->addWidget(rv_header);

            auto* mode_toggle = new QPushButton("Switch to List View");
            mode_toggle->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 8px; }");
            rv_layout->addWidget(mode_toggle);

            auto* stacked = new QStackedWidget();

            auto* folder_edit = new QTextEdit();
            QStringList sorted_folders = new_folders.values();
            sorted_folders.sort();
            folder_edit->setPlainText(sorted_folders.join("\n"));
            folder_edit->setStyleSheet("QTextEdit { background: #1a1a2e; color: #e0e0e0; font-family: monospace; font-size: 11px; }");
            stacked->addWidget(folder_edit);

            auto* folder_list = new QListWidget();
            folder_list->setStyleSheet("QListWidget { background: #1a1a2e; color: #e0e0e0; font-size: 11px; }");
            folder_list->setDragDropMode(QAbstractItemView::InternalMove);
            for (const QString& f : sorted_folders) {
                auto* item = new QListWidgetItem(f);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                folder_list->addItem(item);
            }
            stacked->addWidget(folder_list);
            stacked->setCurrentIndex(0);
            rv_layout->addWidget(stacked, 1);

            connect(mode_toggle, &QPushButton::clicked, [stacked, mode_toggle, folder_edit, folder_list]() {
                if (stacked->currentIndex() == 0) {
                    folder_list->clear();
                    QStringList lines = folder_edit->toPlainText().split('\n', Qt::SkipEmptyParts);
                    for (const QString& line : lines) {
                        if (!line.trimmed().isEmpty()) {
                            auto* item = new QListWidgetItem(line.trimmed());
                            item->setFlags(item->flags() | Qt::ItemIsEditable);
                            folder_list->addItem(item);
                        }
                    }
                    stacked->setCurrentIndex(1);
                    mode_toggle->setText("Switch to Text View");
                } else {
                    QStringList lines;
                    for (int i = 0; i < folder_list->count(); ++i) {
                        lines << folder_list->item(i)->text();
                    }
                    folder_edit->setPlainText(lines.join("\n"));
                    stacked->setCurrentIndex(0);
                    mode_toggle->setText("Switch to List View");
                }
            });

            auto* rv_note = new QLabel("One folder path per line. Empty lines will be ignored.");
            rv_note->setStyleSheet("color: #95a5a6; font-size: 10px;");
            rv_layout->addWidget(rv_note);

            auto* rv_btns = new QHBoxLayout();
            auto* rv_cancel = new QPushButton("Cancel (use as-is)");
            connect(rv_cancel, &QPushButton::clicked, &review_dlg, &QDialog::reject);
            rv_btns->addWidget(rv_cancel);
            rv_btns->addStretch();
            auto* rv_ok = new QPushButton("Apply Changes");
            rv_ok->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 6px 16px; border-radius: 4px; }");
            connect(rv_ok, &QPushButton::clicked, &review_dlg, &QDialog::accept);
            rv_btns->addWidget(rv_ok);
            rv_layout->addLayout(rv_btns);

            if (review_dlg.exec() == QDialog::Accepted) {
                new_folders.clear();
                QStringList lines;
                if (stacked->currentIndex() == 0) {
                    lines = folder_edit->toPlainText().split('\n', Qt::SkipEmptyParts);
                } else {
                    for (int i = 0; i < folder_list->count(); ++i) {
                        lines << folder_list->item(i)->text();
                    }
                }
                QSet<QString> seen;
                for (const QString& line : lines) {
                    QString trimmed = line.trimmed();
                    if (trimmed.isEmpty() || trimmed == source_folder_) continue;
                    if (seen.contains(trimmed)) continue;
                    seen.insert(trimmed);
                    if (!trimmed.startsWith(source_folder_)) {
                        trimmed = QDir::cleanPath(source_folder_ + "/" + trimmed);
                    }
                    new_folders.insert(trimmed);
                }
            }
            log(QString("Categories after review: %1 folder(s)").arg(new_folders.size()));
        }

        if (folder_model_ && !new_folders.isEmpty()) {
            folder_model_->blockSignals(true);
            for (const QString& folder : new_folders) {
                bool is_virtual = !QDir(folder).exists();
                folder_model_->add_folder(folder, is_virtual);
            }
            folder_model_->blockSignals(false);
            if (mind_map_view_) mind_map_view_->refresh_layout();
            log(QString("Added %1 new folder(s) to the grid").arg(new_folders.size()));
        }
    }

    // Wait a moment so user can see the completion message
    QTimer::singleShot(1500, &progress_dialog, &QDialog::accept);
    progress_dialog.exec();

    // Enable re-run button after first analysis
    if (rerun_ai_btn_) rerun_ai_btn_->setEnabled(true);

    // Apply results based on mode
    if (sort_mode_ == AiSortMode::Auto) {
        apply_auto_suggestions();
    } else {
        apply_semi_suggestions();
    }
}

QString AiFileTinderDialog::build_analysis_prompt(const QStringList& file_descriptions,
                                                   const QStringList& available_folders) {
    QString prompt;
    int suggestion_count = (sort_mode_ == AiSortMode::Semi) ? semi_count_ : 1;

    prompt += "You are a file organization assistant with expertise in taxonomy and categorization. ";
    prompt += "Analyze each file carefully \xe2\x80\x94 investigate its name, extension, size, and MIME type ";
    prompt += "to determine precisely what it is and where it belongs. ";
    prompt += "Don't just sort by extension \xe2\x80\x94 understand what the file actually IS. ";
    prompt += "For example, 'Serum_x64.dll' (47MB) is not just a DLL \xe2\x80\x94 it's a VST synthesizer plugin (generator type). ";
    prompt += "'receipt_2024_03.pdf' is not just a PDF \xe2\x80\x94 it's a financial receipt.\n\n";

    switch (category_mode_) {
        case AiCategoryMode::KeepExisting:
            prompt += "IMPORTANT: Use ONLY the existing folders listed below. Do NOT create new folders.\n";
            break;
        case AiCategoryMode::GenerateNew:
            prompt += "IGNORE the existing folders. Create entirely new category folders under the source folder. ";
            prompt += QString("Maximum subcategory depth: %1 levels. ").arg(category_depth_);
            prompt += "Create as many categories as needed for accurate organization.\n";
            break;
        case AiCategoryMode::SynthesizeNew:
            prompt += "Look at the existing folders to understand the user's organizational intent, ";
            prompt += "then create improved categories that blend that intent with fresh analysis. ";
            prompt += QString("Maximum subcategory depth: %1 levels. ").arg(category_depth_);
            prompt += "Create as many categories as needed for accurate organization.\n";
            break;
        case AiCategoryMode::KeepPlusGenerate:
            prompt += "Keep ALL existing folders AND add new ones as needed. ";
            prompt += QString("Maximum subcategory depth: %1 levels. ").arg(category_depth_);
            prompt += "Create as many categories as needed for accurate organization.\n";
            break;
    }

    if (!folder_purpose_.isEmpty()) {
        prompt += QString("\nFolder purpose (from user): \"%1\"\n").arg(folder_purpose_);
    }

    prompt += "\nSource folder (root): " + source_folder_ + "\n";

    if (!available_folders.isEmpty() && category_mode_ != AiCategoryMode::GenerateNew) {
        prompt += "\nExisting folders:\n";
        for (const QString& f : available_folders) {
            prompt += "  - " + f + "\n";
        }
    }

    prompt += QString("\nFor each file, suggest the top %1 best-matching folder(s), ordered by confidence.\n")
        .arg(suggestion_count);
    prompt += "New folder paths MUST be under the source folder root.\n";
    prompt += "IMPORTANT: Use spaces in folder names, not underscores or camelCase. Example: 'Audio Plugins' not 'Audio_Plugins'.\n\n";

    // Include correction feedback if we have any (learning from user overrides)
    if (!corrections_.empty()) {
        prompt += "USER FEEDBACK from previous sorting (learn from these corrections):\n";
        int shown = 0;
        for (const auto& c : corrections_) {
            if (shown >= 10) break;  // Limit context size
            if (c.file_index >= 0 && c.file_index < static_cast<int>(files_.size())) {
                prompt += QString("  - File '%1': AI suggested '%2' but user chose '%3'\n")
                    .arg(files_[c.file_index].name,
                         QFileInfo(c.ai_suggested).fileName(),
                         QFileInfo(c.user_chose).fileName());
                ++shown;
            }
        }
        prompt += "\n";
    }

    prompt += "Files (format: index|name|extension|size_bytes|mime_type):\n";
    for (const QString& desc : file_descriptions) {
        prompt += desc + "\n";
    }

    prompt += "\nRespond with ONLY a JSON array. Each element:\n";
    prompt += "  {\"i\": <file_index>, \"f\": [\"<full_folder_path>\", ...], \"c\": [<confidence_pct>, ...], \"r\": \"<short_reasoning>\"}\n";
    prompt += "Where \"c\" is an array of confidence percentages (0-100) matching each folder in \"f\".\n";
    prompt += "Example: [{\"i\":0,\"f\":[\"" + source_folder_ + "/Images\"],\"c\":[92],\"r\":\"JPEG photo\"}]\n";
    prompt += "Return ONLY the JSON array, no markdown, no explanation.\n";

    return prompt;
}

QString AiFileTinderDialog::send_api_request(const QString& prompt, QString& error_out) {
    QUrl url(provider_config_.endpoint_url);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    int timeout = provider_config_.is_local ? kLocalTimeoutMs : kCloudTimeoutMs;
    request.setTransferTimeout(timeout);

    // Set auth headers
    if (!provider_config_.api_key.isEmpty()) {
        if (provider_config_.provider_name == "Anthropic") {
            request.setRawHeader("x-api-key", provider_config_.api_key.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (provider_config_.provider_name != "Google Gemini") {
            request.setRawHeader("Authorization",
                QByteArray("Bearer ") + provider_config_.api_key.toUtf8());
        }
    }

    // Build request body
    QJsonObject body;

    if (provider_config_.provider_name == "Anthropic") {
        body["model"] = provider_config_.model_name;
        body["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = prompt;
        messages.append(msg);
        body["messages"] = messages;
    } else if (provider_config_.provider_name == "Google Gemini") {
        // Gemini uses different URL/body structure
        QString gemini_url = QString("%1/%2:generateContent?key=%3")
            .arg(provider_config_.endpoint_url,
                 provider_config_.model_name,
                 provider_config_.api_key);
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
        // OpenAI-compatible (OpenAI, Groq, OpenRouter, Mistral, Ollama, LM Studio)
        body["model"] = provider_config_.model_name;
        body["temperature"] = 0.3;
        body["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = QString("You are a file organization assistant. Respond only with the exact JSON array requested. No markdown formatting.");
        messages.append(sys_msg);
        QJsonObject user_msg;
        user_msg["role"] = "user";
        user_msg["content"] = prompt;
        messages.append(user_msg);
        body["messages"] = messages;
    }

    requests_this_minute_++;
    last_request_ms_ = QDateTime::currentMSecsSinceEpoch();

    // Smart retry loop: automatic backoff on 429 (rate limited)
    int max_retries = is_free_tier_ ? 4 : 2;
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        QNetworkReply* reply = network_manager_->post(request, QJsonDocument(body).toJson());

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (http_status == 429 && attempt < max_retries) {
            // Rate limited — exponential backoff
            consecutive_429s_++;
            int retry_after = 5;
            QByteArray retry_hdr = reply->rawHeader("Retry-After");
            if (!retry_hdr.isEmpty()) retry_after = qMax(retry_hdr.toInt(), 1);
            // Exponential backoff: retry_after * 2^attempt, capped at 120s
            int backoff_secs = qMin(retry_after * (1 << attempt), 120);
            reply->deleteLater();

            LOG_INFO("AIMode", QString("429 rate limited, retry %1/%2 in %3s")
                .arg(attempt + 1).arg(max_retries).arg(backoff_secs));
            QEventLoop backoff_loop;
            QTimer::singleShot(backoff_secs * 1000, &backoff_loop, &QEventLoop::quit);
            backoff_loop.exec();
            continue;
        }

        if (reply->error() != QNetworkReply::NoError) {
            error_out = reply->errorString();
            QString resp_body = reply->readAll();
            if (!resp_body.isEmpty()) {
                error_out += " -- " + resp_body.left(300);
            }
            reply->deleteLater();
            return {};
        }

        // Success — decay 429 counter (gradual recovery instead of hard reset)
        consecutive_429s_ = qMax(0, consecutive_429s_ - 1);

        QByteArray response_data = reply->readAll();
        reply->deleteLater();

        QJsonDocument doc = QJsonDocument::fromJson(response_data);
        QString content_text;

        if (provider_config_.provider_name == "Anthropic") {
            QJsonArray content_arr = doc.object()["content"].toArray();
            if (!content_arr.isEmpty()) {
                content_text = content_arr[0].toObject()["text"].toString();
            }
        } else if (provider_config_.provider_name == "Google Gemini") {
            QJsonArray candidates = doc.object()["candidates"].toArray();
            if (!candidates.isEmpty()) {
                QJsonObject content = candidates[0].toObject()["content"].toObject();
                QJsonArray parts = content["parts"].toArray();
                if (!parts.isEmpty()) {
                    content_text = parts[0].toObject()["text"].toString();
                }
            }
        } else {
            QJsonArray choices = doc.object()["choices"].toArray();
            if (!choices.isEmpty()) {
                content_text = choices[0].toObject()["message"].toObject()["content"].toString();
            }
        }

        if (content_text.isEmpty()) {
            error_out = "Empty response from AI";
            return {};
        }

        return content_text;
    }

    // All retries exhausted
    error_out = "Rate limited after multiple retries";
    return {};
}

std::vector<AiFileSuggestion> AiFileTinderDialog::parse_ai_response(
    const QString& response) {

    std::vector<AiFileSuggestion> results;

    auto parse_suggestion = [this](const QJsonObject& obj) -> AiFileSuggestion {
        AiFileSuggestion s;
        s.file_index = obj["i"].toInt(-1);
        if (s.file_index < 0 || s.file_index >= static_cast<int>(files_.size())) {
            s.file_index = -1;
            return s;
        }
        QJsonArray folders = obj["f"].toArray();
        for (const QJsonValue& fv : folders) {
            QString folder = fv.toString();
            if (!folder.isEmpty()) s.suggested_folders.append(folder);
        }
        // Parse confidence scores (optional)
        QJsonArray conf_arr = obj["c"].toArray();
        for (const QJsonValue& cv : conf_arr) {
            s.confidence_scores.append(qBound(0, cv.toInt(50), 100));
        }
        // Pad confidence to match folder count if fewer provided
        while (s.confidence_scores.size() < s.suggested_folders.size()) {
            s.confidence_scores.append(50);
        }
        s.reasoning = obj["r"].toString();
        return s;
    };

    // Level 1: Try full JSON parse
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            auto s = parse_suggestion(val.toObject());
            if (s.file_index >= 0 && !s.suggested_folders.isEmpty())
                results.push_back(s);
        }
        if (!results.empty()) return results;
    }

    // Level 2: Extract JSON array from surrounding text
    int start = response.indexOf('[');
    int end = response.lastIndexOf(']');
    if (start >= 0 && end > start) {
        QString json_str = response.mid(start, end - start + 1);
        doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                auto s = parse_suggestion(val.toObject());
                if (s.file_index >= 0 && !s.suggested_folders.isEmpty())
                    results.push_back(s);
            }
            if (!results.empty()) return results;
        }
    }

    // Level 3: Line-by-line JSON object parsing
    QStringList lines = response.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.startsWith('{')) continue;
        if (trimmed.endsWith(',')) trimmed.chop(1);

        QJsonDocument line_doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (!line_doc.isObject()) continue;

        auto s = parse_suggestion(line_doc.object());
        if (s.file_index >= 0 && !s.suggested_folders.isEmpty())
            results.push_back(s);
    }

    if (!results.empty()) {
        LOG_INFO("AIMode", QString("Level 3 parse recovered %1 suggestions").arg(results.size()));
    } else {
        LOG_ERROR("AIMode", QString("All parse levels failed for response: %1").arg(response.left(500)));
    }

    return results;
}

void AiFileTinderDialog::apply_auto_suggestions() {
    // Build set of valid folders for validation
    QSet<QString> valid_folders;
    valid_folders.insert(source_folder_);
    if (folder_model_) {
        for (const QString& p : folder_model_->get_all_folder_paths())
            valid_folders.insert(p);
    }

    for (const auto& s : suggestions_) {
        if (s.file_index < 0 || s.file_index >= static_cast<int>(files_.size())) continue;

        auto& file = files_[s.file_index];
        if (file.decision != "pending") continue;
        if (s.suggested_folders.isEmpty()) continue;

        QString dest = s.suggested_folders.first();

        // Validate: in KeepExisting mode, dest must be a known folder;
        // in generation modes, dest must be under source_folder_
        if (category_mode_ == AiCategoryMode::KeepExisting) {
            if (!valid_folders.contains(dest)) continue;
        } else {
            if (!dest.startsWith(source_folder_)) continue;
        }

        if (dest == source_folder_) {
            file.decision = "keep";
            keep_count_++;
        } else {
            file.decision = "move";
            file.destination_folder = dest;
            move_count_++;
            if (folder_model_) folder_model_->assign_file_to_folder(dest);
        }
    }

    save_session_state();
    update_stats();
    LOG_INFO("AIMode", "Auto mode complete \xe2\x80\x94 showing review screen");

    update_progress();
    show_review_summary();
}

void AiFileTinderDialog::apply_semi_suggestions() {
    LOG_INFO("AIMode", QString("Semi mode \xe2\x80\x94 %1 suggestions ready").arg(suggestions_.size()));
    current_filtered_index_ = 0;
    update_progress();
    show_current_file();
}

void AiFileTinderDialog::show_current_file() {
    clear_folder_highlights();
    AdvancedFileTinderDialog::show_current_file();

    // In semi mode, highlight AI-suggested folders
    if (sort_mode_ == AiSortMode::Semi && !suggestions_.empty()) {
        int file_idx = get_current_file_index();
        if (file_idx >= 0) {
            for (const auto& s : suggestions_) {
                if (s.file_index == file_idx) {
                    highlight_suggested_folders(s.suggested_folders);
                    // Show reasoning
                    if (ai_reasoning_label_) {
                        if (!s.reasoning.isEmpty() && is_reasoning_visible()) {
                            ai_reasoning_label_->setText("AI: " + s.reasoning);
                            ai_reasoning_label_->setVisible(true);
                        } else {
                            ai_reasoning_label_->setVisible(false);
                        }
                    }
                    // Show confidence scores on suggestions list items
                    if (ai_suggestions_list_ && !s.confidence_scores.isEmpty()) {
                        for (int i = 0; i < ai_suggestions_list_->count() && i < s.confidence_scores.size(); ++i) {
                            auto* item = ai_suggestions_list_->item(i);
                            if (!item) continue;
                            int conf = s.confidence_scores[i];
                            // Skip suggestions below threshold
                            if (confidence_threshold_ > 0 && conf < confidence_threshold_) {
                                item->setHidden(true);
                                continue;
                            }
                            // Color by confidence: green >80, yellow 50-80, red <50
                            QString color = conf >= 80 ? "#2ecc71" : (conf >= 50 ? "#f1c40f" : "#e74c3c");
                            item->setText(item->text() + QString(" %1%").arg(conf));
                            item->setForeground(QColor(color));
                        }
                    }
                    break;
                }
            }
        }
    }

    // Show AI reasoning for current file (all modes)
    if (ai_reasoning_label_) {
        int file_idx = get_current_file_index();
        bool found = false;
        for (const auto& s : suggestions_) {
            if (s.file_index == file_idx && !s.reasoning.isEmpty()) {
                ai_reasoning_label_->setText(QString("AI: %1").arg(s.reasoning));
                ai_reasoning_label_->setVisible(is_reasoning_visible());
                found = true;
                break;
            }
        }
        if (!found) {
            ai_reasoning_label_->setVisible(false);
        }
    }
}

bool AiFileTinderDialog::is_reasoning_visible() const {
    return !ai_reasoning_toggle_ || ai_reasoning_toggle_->isChecked();
}

void AiFileTinderDialog::highlight_suggested_folders(const QStringList& folders) {
    highlighted_folders_ = folders;

    // Highlight best match in the mind map
    if (mind_map_view_ && !folders.isEmpty()) {
        mind_map_view_->set_selected_folder(folders.first());
    }

    // Apply glow borders on grid nodes if toggle is on
    if (mind_map_view_ && (!ai_glow_toggle_ || ai_glow_toggle_->isChecked())) {
        mind_map_view_->set_highlighted_paths(folders);
    }

    // Populate the AI Suggestions panel (separate from Quick Access)
    if (ai_suggestions_list_) {
        ai_suggestions_list_->clear();
        for (int i = 0; i < folders.size(); ++i) {
            QString path = folders[i];
            QString name = QFileInfo(path).fileName();
            if (name.length() > 18) name = name.left(17) + QString::fromUtf8("\xe2\x80\xa6");
            QString label = QString("%1. %2").arg(i + 1).arg(name);

            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            item->setSizeHint(QSize(ui::scaling::scaled(140), ui::scaling::scaled(28)));
            ai_suggestions_list_->addItem(item);
        }
        if (ai_suggestions_panel_) ai_suggestions_panel_->setVisible(!folders.isEmpty());
    }
}

void AiFileTinderDialog::clear_folder_highlights() {
    highlighted_folders_.clear();
    if (mind_map_view_) {
        mind_map_view_->clear_highlighted_paths();
    }
    if (ai_suggestions_list_) {
        ai_suggestions_list_->clear();
    }
    if (ai_suggestions_panel_) {
        ai_suggestions_panel_->setVisible(false);
    }
    if (ai_reasoning_label_) {
        ai_reasoning_label_->setVisible(false);
    }
}

void AiFileTinderDialog::on_folder_clicked_from_ai(const QString& folder_path) {
    int file_idx = get_current_file_index();
    if (file_idx < 0 || file_idx >= static_cast<int>(files_.size())) return;

    // Track AI correction — if user chose a different folder than AI's top suggestion
    if (sort_mode_ == AiSortMode::Semi) {
        for (const auto& s : suggestions_) {
            if (s.file_index == file_idx && !s.suggested_folders.isEmpty()) {
                if (s.suggested_folders.first() != folder_path) {
                    AiCorrection correction;
                    correction.file_index = file_idx;
                    correction.ai_suggested = s.suggested_folders.first();
                    correction.user_chose = folder_path;
                    corrections_.push_back(correction);
                    LOG_INFO("AIMode", QString("Correction: file %1, AI suggested '%2', user chose '%3'")
                        .arg(file_idx).arg(s.suggested_folders.first(), folder_path));
                }
                break;
            }
        }
    }

    auto& file = files_[file_idx];
    QString old_decision = file.decision;
    record_action(file_idx, old_decision, "move", folder_path);
    update_decision_count(old_decision, -1);

    file.decision = "move";
    file.destination_folder = folder_path;
    move_count_++;

    if (folder_model_) folder_model_->assign_file_to_folder(folder_path);
    update_stats();
    save_session_state();
    advance_to_next();
}

bool AiFileTinderDialog::check_rate_limit() {
    return requests_this_minute_ < provider_config_.rate_limit_rpm;
}

void AiFileTinderDialog::reset_rate_limit() {
    requests_this_minute_ = 0;
}
