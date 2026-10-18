#include "localization.h"
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTest>
#include <QTranslator>
#include <QXmlStreamReader>

class LocalizationTest : public QObject {
    Q_OBJECT
private slots:
    void compiledCatalogMatchesSource()
    {
        QFile file(QStringLiteral(DISKLENS_SOURCE_DIR "/translations/disklens_en.ts"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/translations/disklens_en.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const QRegularExpression placeholder(QStringLiteral("%(?:L?\\d+|n)"));
        const auto tokens = [&placeholder](const QString &text) {
            QStringList result;
            auto matches = placeholder.globalMatch(text);
            while (matches.hasNext()) result.append(matches.next().captured());
            result.sort();
            return result;
        };
        int count = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != QStringLiteral("message")) continue;
            QString source, expected;
            while (xml.readNextStartElement()) {
                if (xml.name() == QStringLiteral("source")) source = xml.readElementText();
                else if (xml.name() == QStringLiteral("translation")) {
                    QVERIFY(xml.attributes().value(QStringLiteral("type")) != QStringLiteral("unfinished"));
                    expected = xml.readElementText();
                } else xml.skipCurrentElement();
            }
            QVERIFY(!source.isEmpty());
            QVERIFY2(!expected.isEmpty(), qPrintable(source));
            QCOMPARE(tokens(expected), tokens(source));
            const QByteArray utf8 = source.toUtf8();
            QCOMPARE(dlTr(utf8.constData()), expected);
            ++count;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(count >= 540);
        QVERIFY(QCoreApplication::removeTranslator(&translator));
        QCOMPARE(dlTr("缓存与日志"), QStringLiteral("缓存与日志"));
    }
};

QTEST_GUILESS_MAIN(LocalizationTest)
#include "localization_test.moc"
