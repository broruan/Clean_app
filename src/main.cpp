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
    QFont font("Noto Sans CJK SC",10);application.setFont(font);application.setStyleSheet(applicationStyle());
    QCommandLineParser parser;parser.setApplicationDescription(QStringLiteral("磁盘透镜：本机软件占用与大文件可视化。适用于 Ubuntu / Debian。"));parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"scan-path",QStringLiteral("启动时扫描的目录，默认用户主目录"),"directory"});
    parser.addOption({"smoke-test",QStringLiteral("运行只读界面检查后退出，不执行卸载或授权")});
    parser.addOption({"screenshot",QStringLiteral("检查完成后保存界面截图；配合 --smoke-test 使用"),"path"});
    parser.addOption({"page",QStringLiteral("启动 / 截图页面：overview、apps、files、cache、explorer、duplicates、developer"),"name","overview"});
    parser.process(application);
    MainWindow window;if(parser.isSet("scan-path"))window.setScanRoot(parser.value("scan-path"));window.show();
    const QString page=parser.value("page");const int pageIndex=page=="apps"?1:page=="files"?2:page=="cache"?3:(page=="explorer"||page=="duplicates")?4:page=="developer"?5:0;
    auto navigation=window.findChildren<QPushButton*>("navButton");if(pageIndex<navigation.size())navigation[pageIndex]->click();
    if(page=="duplicates") {
        auto *explorer=window.findChild<ExplorerPage*>();
        if(explorer){explorer->showDuplicates();explorer->startDuplicateScan();}
    }
    if(parser.isSet("smoke-test")) {
        auto *timer=new QTimer(&window);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
        QObject::connect(timer,&QTimer::timeout,&window,[&application,&window,&parser,timer,elapsed]{
            if(!window.isSettled() && elapsed->elapsed()<90000)return;
            timer->stop();QString error;
            if(!window.verifyUi(&error)){qCritical("Smoke check failed: %s",qPrintable(error));application.exit(1);return;}
            const QString page=parser.value("page");const int index=page=="apps"?1:page=="files"?2:page=="cache"?3:(page=="explorer"||page=="duplicates")?4:page=="developer"?5:0;
            auto *stack=window.findChild<QStackedWidget*>();if(stack)stack->setCurrentIndex(index);
            // Select through the navigation button so header and selected state stay consistent.
            auto buttons=window.findChildren<QPushButton*>("navButton");if(index<buttons.size())buttons[index]->click();
            QTimer::singleShot(150,&window,[&application,&window,&parser]{
                if(parser.isSet("screenshot")) {
                    const QString path=QFileInfo(parser.value("screenshot")).absoluteFilePath();
                    if(!window.grab().save(path)){qCritical("Could not save screenshot");application.exit(2);return;}
                    qInfo("Screenshot saved: %s",qPrintable(path));
                }
                application.exit(0);
            });
        });timer->start(200);
    }
    return application.exec();
}
