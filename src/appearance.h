#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

class QSettings;
class QWidget;

struct AppearancePreferences {
    QString language = QStringLiteral("zh_CN");
    int fontSize = 13;
    bool operator==(const AppearancePreferences &other) const
    { return language == other.language && fontSize == other.fontSize; }
    bool operator!=(const AppearancePreferences &other) const { return !(*this == other); }
};

// Preferences, translation lifetime and font scaling share one application owner.
// MainWindow is rebuilt only after its read-only workers have been joined.
class AppearanceManager final : public QObject {
    Q_OBJECT
public:
    explicit AppearanceManager(QObject *parent = nullptr);
    ~AppearanceManager() override;
    static AppearancePreferences readSettings(QSettings &settings);
    static AppearancePreferences readSettings();
    static bool valid(const AppearancePreferences &preferences);
    static bool saveSettings(const AppearancePreferences &preferences, QSettings &settings);
    static bool saveSettings(const AppearancePreferences &preferences);
    bool apply(const AppearancePreferences &preferences, bool persist = false, QString *error = nullptr);
    AppearancePreferences preferences() const { return m_preferences; }
    static QString scaledStyleSheet(const QString &styleSheet, int fontSize);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    AppearancePreferences m_preferences;
    QTranslator m_translator;
    QTranslator m_qtTranslator;
};

// Returns false on cancellation. The dialog itself never changes persisted state.
bool chooseAppearance(QWidget *parent, const AppearancePreferences &current, AppearancePreferences *chosen);
