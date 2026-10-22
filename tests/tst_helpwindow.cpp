#include "helpwindow.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>

namespace {

QTextBrowser *browserOf(HelpWindow &window)
{
    auto *browser = window.findChild<QTextBrowser *>();
    if (!browser)
        qFatal("no browser");
    return browser;
}

QStringList anchorNames(const QTextDocument *document)
{
    QStringList names;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it)
            names << it.fragment().charFormat().anchorNames();
    }
    return names;
}

} // namespace

class TestHelpWindow : public QObject {
    Q_OBJECT
private slots:
    void manualIsBuiltIn()
    {
        HelpWindow window;
        const QTextDocument *doc = browserOf(window)->document();
        QVERIFY(doc->toPlainText().contains(QStringLiteral("Sonde Predict 5")));
        QVERIFY(doc->toPlainText().contains(QStringLiteral("Making a prediction")));
    }

    void everyContentsLinkHasAnAnchor()
    {
        HelpWindow window;
        const QStringList anchors = anchorNames(browserOf(window)->document());
        QFile source(QStringLiteral(":/manual/user-manual.md"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QString markdown = QString::fromUtf8(source.readAll());
        const QRegularExpression link(QStringLiteral("\\]\\(#([a-z0-9-]+)\\)"));
        int links = 0;
        for (auto it = link.globalMatch(markdown); it.hasNext(); ++links) {
            const QString target = it.next().captured(1);
            QVERIFY2(anchors.contains(target), qPrintable(QStringLiteral("no heading for #%1").arg(target)));
        }
        QVERIFY(links > 20);
    }

    void picturesLoad()
    {
        QFile source(QStringLiteral(":/manual/user-manual.md"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QRegularExpression image(QStringLiteral("!\\[[^\\]]*\\]\\(([^)]+)\\)"));
        int count = 0;
        for (auto it = image.globalMatch(QString::fromUtf8(source.readAll())); it.hasNext(); ++count) {
            const QString path = QStringLiteral(":/manual/") + it.next().captured(1);
            QVERIFY2(!QImage(path).isNull(), qPrintable(path));
        }
        QVERIFY(count >= 8);
    }

    void writesPdf()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("manual.pdf"));
        QCOMPARE(HelpWindow::writePdf(path), QString());
        QFile pdf(path);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.size() > 100000);
        QVERIFY(pdf.read(5) == "%PDF-");
        QVERIFY(!HelpWindow::writePdf(dir.filePath(QStringLiteral("missing/dir/manual.pdf"))).isEmpty());
    }

    void screenshot()
    {
        const QString dir = qEnvironmentVariable("SP_TEST_SCREENSHOTS");
        if (dir.isEmpty())
            QSKIP("set SP_TEST_SCREENSHOTS to a directory to save a screenshot");
        HelpWindow window;
        window.show();
        QTest::qWait(100);
        browserOf(window)->scrollToAnchor(QStringLiteral("reading-a-prediction"));
        QTest::qWait(100);
        QVERIFY(window.grab().save(QDir(dir).filePath(QStringLiteral("help-window.png"))));
    }
};

QTEST_MAIN(TestHelpWindow)
#include "tst_helpwindow.moc"
