#ifndef AI_FILE_TINDER_DIALOG_HPP
#define AI_FILE_TINDER_DIALOG_HPP

#include "AdvancedFileTinderDialog.hpp"
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QRadioButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QMap>
#include <QTextBrowser>
#include <QCheckBox>

class MindMapView;
class FolderTreeModel;

// Stores an AI provider configuration
struct AiProviderConfig {
    QString provider_name;
    QString api_key;
    QString endpoint_url;
    QString model_name;
    bool is_local = false;
    int rate_limit_rpm = 60;
};

// AI sorting mode
enum class AiSortMode {
    Auto,
    Semi
};

// Category generation mode
enum class AiCategoryMode {
    KeepExisting,
    GenerateNew,
    SynthesizeNew,
    KeepPlusGenerate
};

// Per-file AI suggestion
struct AiFileSuggestion {
    int file_index;
    QStringList suggested_folders;
    QList<int> confidence_scores;  // Percentage (0-100) per folder suggestion
    QString reasoning;
};

// ── AiSetupDialog ──────────────────────────────────────────
class AiSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit AiSetupDialog(const QStringList& existing_folders,
                           int file_count,
                           DatabaseManager& db,
                           const QString& session_folder,
                           QWidget* parent = nullptr);

    AiSortMode sort_mode() const;
    int semi_mode_count() const;
    AiCategoryMode category_mode() const;
    int category_depth() const;
    QString folder_purpose() const;
    AiProviderConfig provider_config() const;
    int confidence_threshold() const;
    int category_limit() const;

private:
    void build_ui();
    void load_saved_provider();
    void save_provider_config();
    void update_cost_estimate();
    void fetch_models(const QString& provider);
    int default_rate_limit(const QString& provider) const;

    DatabaseManager& db_;
    QString session_folder_;
    QStringList existing_folders_;
    int file_count_;

    QComboBox* provider_combo_;
    QLineEdit* api_key_edit_;
    QLineEdit* endpoint_edit_;
    QComboBox* model_combo_;
    QRadioButton* auto_radio_;
    QRadioButton* semi_radio_;
    QSpinBox* semi_count_spin_;
    QComboBox* category_combo_;
    QSpinBox* depth_spin_;
    QCheckBox* category_limit_check_ = nullptr;
    QSpinBox* category_limit_spin_ = nullptr;
    QTextEdit* purpose_edit_;
    QLabel* cost_label_;
    QNetworkAccessManager* fetch_nam_;
};

// ── AiFileTinderDialog ─────────────────────────────────────
class AiFileTinderDialog : public AdvancedFileTinderDialog {
    Q_OBJECT

public:
    explicit AiFileTinderDialog(const QString& source_folder,
                                DatabaseManager& db,
                                QWidget* parent = nullptr);
    ~AiFileTinderDialog() override;

    void initialize() override;

protected:
    void show_current_file() override;

private:
    // AI state
    AiSortMode sort_mode_;
    AiCategoryMode category_mode_;
    int semi_count_;
    int category_depth_;
    int category_limit_ = 0;
    QString folder_purpose_;
    AiProviderConfig provider_config_;
    std::vector<AiFileSuggestion> suggestions_;

    QStringList highlighted_folders_;
    QPushButton* ai_setup_btn_ = nullptr;
    QPushButton* rerun_ai_btn_ = nullptr;
    QCheckBox* ai_glow_toggle_ = nullptr;

    // Network
    QNetworkAccessManager* network_manager_;
    QTimer* rate_limit_timer_;
    int requests_this_minute_;
    bool ai_configured_ = false;
    bool is_free_tier_ = false;
    int consecutive_429s_ = 0;
    qint64 last_request_ms_ = 0;

    QWidget* ai_suggestions_panel_ = nullptr;
    QListWidget* ai_suggestions_list_ = nullptr;
    QPushButton* ai_other_btn_ = nullptr;
    QLabel* ai_reasoning_label_ = nullptr;
    QCheckBox* ai_reasoning_toggle_ = nullptr;
    int confidence_threshold_ = 0;

    // AI correction tracking — records when user overrides AI suggestion
    struct AiCorrection {
        int file_index;
        QString ai_suggested;   // AI's top suggestion
        QString user_chose;     // What the user actually picked
    };
    std::vector<AiCorrection> corrections_;

    // Show AI setup dialog; returns false if user cancelled
    bool show_ai_setup();

    // Run the AI analysis on files (all or remaining only)
    void run_ai_analysis(bool remaining_only);

    // Build the prompt for the AI
    QString build_analysis_prompt(const QStringList& file_descriptions,
                                  const QStringList& available_folders);

    // Parse AI response into suggestions
    std::vector<AiFileSuggestion> parse_ai_response(const QString& response);

    // Send one batch to the API and return the response text (blocking)
    QString send_api_request(const QString& prompt, QString& error_out);

    // Apply auto-mode suggestions (set decisions, go to review)
    void apply_auto_suggestions();

    // Apply semi-mode suggestions (highlight folders for current file)
    void apply_semi_suggestions();

    // Highlight specific folders in the mind map and quick access
    void highlight_suggested_folders(const QStringList& folders);
    void clear_folder_highlights();
    void on_folder_clicked_from_ai(const QString& folder_path);

    // Dynamic batch sizing based on folder count, model type, and file count
    int calculate_batch_size(int folder_count, int total_files) const;

    // Whether reasoning display is enabled via toggle
    bool is_reasoning_visible() const;

    // Rate limiting
    bool check_rate_limit();
    void reset_rate_limit();
};

#endif // AI_FILE_TINDER_DIALOG_HPP
