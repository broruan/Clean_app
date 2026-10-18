#include "localization.h"
#include "appearance.h"
#include "mainwindow.h"
#include "widgets.h"
#include "explorerpage.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPushButton>
#include <QPointer>
#include <QMessageBox>
#include <functional>
#include <QStackedWidget>
#include <QTimer>
#include <memory>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication application(argc,argv);
    application.setApplicationName("DiskLens");application.setApplicationVersion(DISKLENS_VERSION);application.setOrganizationName("DiskLens");
    application.setStyle("Fusion");
    if(QIcon::themeName().isEmpty())QIcon::setThemeName("Yaru");
    QIcon::setFallbackThemeName("hicolor");
    AppearanceManager appearance;
    AppearancePreferences preferences = AppearanceManager::readSettings();
    // Read only appearance overrides before constructing translated CLI help.
    const QStringList arguments = application.arguments();
    struct OverrideValue { bool present=false; QString value; };
    auto overrideValue = [&arguments](const QString &name) {
        OverrideValue result;
        for (int i=1;i<arguments.size();++i) {
            if (arguments[i] == name) {
                result.present=true;
                result.value=i+1<arguments.size() ? arguments[++i] : QStringLiteral("");
            } else if (arguments[i].startsWith(name+"=")) {
                result.present=true;result.value=arguments[i].mid(name.size()+1);
            }
        }
        return result;
    };
    const auto languageOverride=overrideValue(QStringLiteral("--language"));
    const auto fontOverride=overrideValue(QStringLiteral("--font-size"));
    if (languageOverride.present) preferences.language=languageOverride.value;
    if (fontOverride.present) {
        bool ok=false;preferences.fontSize=fontOverride.value.toInt(&ok);
        if(!ok){qCritical("--font-size must be an integer from 11 to 18");return 2;}
    }
    if(!AppearanceManager::valid(preferences)){qCritical("--language must be en or zh_CN; --font-size must be 11..18");return 2;}
    QString appearanceError;
    if(!appearance.apply(preferences,false,&appearanceError)){qCritical("%s",qPrintable(appearanceError));return 2;}
    QCommandLineParser parser;parser.setApplicationDescription(dlTr("磁盘透镜：本机软件占用与大文件可视化。适用于 Ubuntu / Debian。"));parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"language",dlTr("本次运行的界面语言：zh_CN 或 en（不保存）"),"language"});
    parser.addOption({"font-size",dlTr("本次运行的基础字体大小：11–18 px（不保存）"),"pixels"});
    parser.addOption({"scan-path",dlTr("启动时扫描的目录，默认用户主目录"),"directory"});
    parser.addOption({"smoke-test",dlTr("运行只读界面检查后退出，不执行卸载或授权")});
    parser.addOption({"screenshot",dlTr("检查完成后保存界面截图；配合 --smoke-test 使用"),"path"});
    parser.addOption({"page",dlTr("启动 / 截图页面：overview、apps、files、cache、explorer、duplicates、developer"),"name","overview"});
    parser.process(application);
    std::unique_ptr<MainWindow> window;
    std::function<void(const MainWindow::ViewState *)> createWindow;
    createWindow=[&](const MainWindow::ViewState *state) {
        window=std::make_unique<MainWindow>();
        if(state) window->restoreViewState(*state);
        else if(parser.isSet("scan-path")) window->setScanRoot(parser.value("scan-path"));
        const QPointer<MainWindow> source(window.get());
        QObject::connect(window.get(),&MainWindow::appearanceChangeRequested,&application,
            [&,source](const QString &language,int fontSize) {
                QTimer::singleShot(0,&application,[&,source,language,fontSize]{
                    if(!source || source!=window.get() || !source->canChangeAppearance())return;
                    const auto state=source->viewState();
                    application.setQuitOnLastWindowClosed(false);
                    window.reset(); // Cancels and joins every read-only worker before replacing the catalog.
                    QString error;
                    const bool applied=appearance.apply({language,fontSize},true,&error);
                    createWindow(&state);
                    application.setQuitOnLastWindowClosed(true);
                    if(!applied)QMessageBox::warning(window.get(),dlTr("无法应用外观设置"),error);
                });
            });
        window->show();
    };
    createWindow(nullptr);
    const QString page=parser.value("page");const int pageIndex=page=="apps"?1:page=="files"?2:page=="cache"?3:(page=="explorer"||page=="duplicates")?4:page=="developer"?5:0;
    auto navigation=window->findChildren<QPushButton*>("navButton");if(pageIndex<navigation.size())navigation[pageIndex]->click();
    if(page=="duplicates") {
        auto *explorer=window->findChild<ExplorerPage*>();
        if(explorer){explorer->showDuplicates();explorer->startDuplicateScan();}
    }
    if(parser.isSet("smoke-test")) {
        auto *timer=new QTimer(window.get());auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
        QObject::connect(timer,&QTimer::timeout,window.get(),[&application,&window,&parser,timer,elapsed]{
            if(!window->isSettled() && elapsed->elapsed()<90000)return;
            timer->stop();QString error;
            if(!window->verifyUi(&error)){qCritical("Smoke check failed: %s",qPrintable(error));application.exit(1);return;}
            const QString page=parser.value("page");const int index=page=="apps"?1:page=="files"?2:page=="cache"?3:(page=="explorer"||page=="duplicates")?4:page=="developer"?5:0;
            auto *stack=window->findChild<QStackedWidget*>();if(stack)stack->setCurrentIndex(index);
            // Select through the navigation button so header and selected state stay consistent.
            auto buttons=window->findChildren<QPushButton*>("navButton");if(index<buttons.size())buttons[index]->click();
            QTimer::singleShot(150,window.get(),[&application,&window,&parser]{
                if(parser.isSet("screenshot")) {
                    const QString path=QFileInfo(parser.value("screenshot")).absoluteFilePath();
                    if(!window->grab().save(path)){qCritical("Could not save screenshot");application.exit(2);return;}
                    qInfo("Screenshot saved: %s",qPrintable(path));
                }
                application.exit(0);
            });
        });timer->start(200);
    }
    return application.exec();
}
