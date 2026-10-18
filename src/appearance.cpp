#include "appearance.h"
#include "localization.h"
#include "widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLibraryInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

AppearanceManager::AppearanceManager(QObject *parent) : QObject(parent)
{
    qApp->installEventFilter(this);
}
AppearanceManager::~AppearanceManager()
{
    qApp->removeEventFilter(this);
    qApp->removeTranslator(&m_translator);
    qApp->removeTranslator(&m_qtTranslator);
}
AppearancePreferences AppearanceManager::readSettings(QSettings &settings)
{
    AppearancePreferences result;
    const QString language = settings.value(QStringLiteral("appearance/language"), result.language).toString();
    const int fontSize = settings.value(QStringLiteral("appearance/fontSize"), result.fontSize).toInt();
    if (language == QStringLiteral("en") || language == QStringLiteral("zh_CN")) result.language = language;
    if (fontSize >= 11 && fontSize <= 18) result.fontSize = fontSize;
    return result;
}
AppearancePreferences AppearanceManager::readSettings()
{
    QSettings settings;
    return readSettings(settings);
}
bool AppearanceManager::valid(const AppearancePreferences &preferences)
{
    return (preferences.language == QStringLiteral("en") || preferences.language == QStringLiteral("zh_CN"))
        && preferences.fontSize >= 11 && preferences.fontSize <= 18;
}
bool AppearanceManager::saveSettings(const AppearancePreferences &preferences, QSettings &settings)
{
    if (!valid(preferences)) return false;
    settings.setValue(QStringLiteral("appearance/language"), preferences.language);
    settings.setValue(QStringLiteral("appearance/fontSize"), preferences.fontSize);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
bool AppearanceManager::saveSettings(const AppearancePreferences &preferences)
{
    QSettings settings;
    return saveSettings(preferences, settings);
}
bool AppearanceManager::apply(const AppearancePreferences &preferences, bool persist, QString *error)
{
    if (!valid(preferences)) {
        if (error) *error = dlTr("语言或字体大小设置无效。");
        return false;
    }
    // Loading first leaves the previous language intact if a resource is absent.
    if (preferences.language == QStringLiteral("en") && !m_translator.load(QStringLiteral(":/translations/disklens_en.qm"))) {
        if (error) *error = dlTr("无法加载英文语言资源。");
        return false;
    }
    if (persist && !saveSettings(preferences)) {
        if (error) *error = dlTr("无法保存外观设置，请检查配置目录的写入权限。");
        return false;
    }
    qApp->removeTranslator(&m_translator);
    qApp->removeTranslator(&m_qtTranslator);
    if (preferences.language == QStringLiteral("zh_CN")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
        const QString translationsPath = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
        // Qt's system catalog localizes standard file dialogs and message buttons.
        // Absence is nonfatal: all DiskLens-owned strings remain translated.
        if (m_qtTranslator.load(QStringLiteral("qtbase_zh_CN"), translationsPath))
            qApp->installTranslator(&m_qtTranslator);
    }
    if (preferences.language == QStringLiteral("en")) qApp->installTranslator(&m_translator);
    m_preferences = preferences;
    qApp->setProperty("disklensLanguage", preferences.language);
    qApp->setProperty("disklensFontSize", preferences.fontSize);
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPixelSize(preferences.fontSize);
    qApp->setFont(font);
    qApp->setStyleSheet(scaledStyleSheet(applicationStyle(), preferences.fontSize));
    return true;
}
QString AppearanceManager::scaledStyleSheet(const QString &styleSheet, int fontSize)
{
    const QRegularExpression pattern(QStringLiteral("(font-size\\s*:\\s*)([0-9]+(?:\\.[0-9]+)?)(px)"));
    QString result;
    qsizetype position = 0;
    auto matches = pattern.globalMatch(styleSheet);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += styleSheet.mid(position, match.capturedStart() - position);
        result += match.captured(1) + QString::number(qRound(match.captured(2).toDouble() * fontSize / 13.0)) + match.captured(3);
        position = match.capturedEnd();
    }
    result += styleSheet.mid(position);
    return result;
}
bool AppearanceManager::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::Polish) {
        if (auto *widget = qobject_cast<QWidget *>(object)) {
            if (!widget->property("disklensAppearancePolished").toBool()) {
                widget->setProperty("disklensAppearancePolished", true);
                if (!widget->styleSheet().isEmpty())
                    widget->setStyleSheet(scaledStyleSheet(widget->styleSheet(), m_preferences.fontSize));
                if (auto *table = qobject_cast<QTableView *>(widget)) {
                    const int height = table->verticalHeader()->defaultSectionSize();
                    table->verticalHeader()->setDefaultSectionSize(qRound(height * m_preferences.fontSize / 13.0));
                }
            }
        }
    }
    return QObject::eventFilter(object, event);
}
bool chooseAppearance(QWidget *parent, const AppearancePreferences &current, AppearancePreferences *chosen)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("appearanceDialog"));
    dialog.setWindowTitle(dlTr("语言与字体"));
    dialog.setMinimumWidth(430);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(18);
    auto *form = new QFormLayout;
    form->setSpacing(14);
    auto *language = new QComboBox;
    language->setObjectName(QStringLiteral("appearanceLanguage"));
    language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->setCurrentIndex(language->findData(current.language));
    form->addRow(dlTr("界面语言"), language);
    auto *fontSize = new QComboBox;
    fontSize->setObjectName(QStringLiteral("appearanceFontSize"));
    for (int size : {11, 12, 13, 14, 15, 16, 17, 18})
        fontSize->addItem(size == 13 ? dlTr("%1 px（默认）").arg(size) : QStringLiteral("%1 px").arg(size), size);
    fontSize->setCurrentIndex(fontSize->findData(current.fontSize));
    form->addRow(dlTr("字体大小"), fontSize);
    layout->addLayout(form);
    auto *preview = new QLabel(QStringLiteral("DiskLens · 磁盘透镜 · Aa 123"));
    preview->setObjectName(QStringLiteral("appearancePreview"));
    preview->setProperty("disklensAppearancePolished", true);
    preview->setMinimumHeight(52);
    preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview);
    const auto updatePreview = [preview, fontSize] {
        preview->setStyleSheet(QStringLiteral("color:#29364f;background:#edf2ff;border-radius:7px;font-size:%1px;")
            .arg(fontSize->currentData().toInt()));
    };
    QObject::connect(fontSize, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updatePreview);
    updatePreview();
    auto *notice = new QLabel(dlTr("设置立即生效并自动保存。只读扫描会重新开始；当前页面、扫描目录和窗口位置会保留。"));
    notice->setWordWrap(true);
    notice->setObjectName(QStringLiteral("muted"));
    layout->addWidget(notice);
    auto *buttons = new QDialogButtonBox;
    auto *apply = buttons->addButton(dlTr("应用"), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("appearanceApply"));
    auto *cancel = buttons->addButton(dlTr("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("appearanceCancel"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    if (chosen) *chosen = {language->currentData().toString(), fontSize->currentData().toInt()};
    return true;
}
