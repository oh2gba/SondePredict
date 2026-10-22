#include "helpwindow.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QPageLayout>
#include <QPdfWriter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <vector>

namespace help {

namespace {

QString slug(const QString &heading)
{
    QString s = heading.toLower();
    s.remove(QRegularExpression(QStringLiteral("[^\\w\\- ]"), QRegularExpression::UseUnicodePropertiesOption));
    s.replace(u' ', u'-');
    return s;
}

} // namespace

void prepareDocument(QTextDocument *document, int maxImageWidth)
{
    QTextCursor cursor(document);
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0 && !block.text().isEmpty()) {
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            QTextCharFormat anchor;
            anchor.setAnchor(true);
            anchor.setAnchorNames({slug(block.text())});
            cursor.mergeCharFormat(anchor);
        }
    }

    // Collect first: changing formats while iterating would invalidate the fragments.
    struct Image {
        int position;
        int length;
        QTextImageFormat format;
    };
    std::vector<Image> images;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                images.push_back({fragment.position(), fragment.length(), fragment.charFormat().toImageFormat()});
        }
    }
    for (Image &image : images) {
        // Images are loaded lazily; fetch any not loaded yet straight from the resources.
        const QUrl url(image.format.name());
        QImage picture = document->resource(QTextDocument::ImageResource, url).value<QImage>();
        if (picture.isNull()) {
            picture = QImage(QStringLiteral(":/manual/") + image.format.name());
            if (!picture.isNull())
                document->addResource(QTextDocument::ImageResource, url, picture);
        }
        if (picture.isNull() || picture.width() <= maxImageWidth)
            continue;
        image.format.setWidth(maxImageWidth);
        image.format.setHeight(double(picture.height()) * maxImageWidth / picture.width());
        cursor.setPosition(image.position);
        cursor.setPosition(image.position + image.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(image.format);
    }
}

} // namespace help

HelpWindow::HelpWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Sonde Predict user manual"));
    resize(860, 760);

    m_browser = new QTextBrowser;
    m_browser->setOpenExternalLinks(true);
    connect(m_browser, &QTextBrowser::sourceChanged, this, [this]() { help::prepareDocument(m_browser->document(), 760); });
    m_browser->setSource(QUrl(QString::fromLatin1(help::kManualUrl)), QTextDocument::MarkdownResource);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *back = buttons->addButton(tr("&Back"), QDialogButtonBox::ActionRole);
    QPushButton *contents = buttons->addButton(tr("&Contents"), QDialogButtonBox::ActionRole);
    QPushButton *pdf = buttons->addButton(tr("Save as &PDF..."), QDialogButtonBox::ActionRole);
    back->setEnabled(false);
    connect(m_browser, &QTextBrowser::backwardAvailable, back, &QWidget::setEnabled);
    connect(back, &QPushButton::clicked, m_browser, &QTextBrowser::backward);
    connect(contents, &QPushButton::clicked, m_browser, [this]() { m_browser->scrollToAnchor(QStringLiteral("contents")); });
    connect(pdf, &QPushButton::clicked, this, &HelpWindow::saveAsPdf);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_browser);
    layout->addWidget(buttons);
}

QString HelpWindow::writePdf(const QString &path)
{
    QFile source(QString::fromLatin1(help::kManualUrl).mid(3)); // ":/manual/user-manual.md"
    if (!source.open(QIODevice::ReadOnly))
        return tr("the manual is missing from this build");

    QTextBrowser browser; // loads the images from the resources
    browser.setSource(QUrl(QString::fromLatin1(help::kManualUrl)), QTextDocument::MarkdownResource);
    QTextDocument *document = browser.document();

    QFile target(path);
    if (!target.open(QIODevice::WriteOnly))
        return tr("cannot write %1: %2").arg(path, target.errorString());
    QPdfWriter writer(&target);
    writer.setTitle(tr("Sonde Predict user manual"));
    writer.setCreator(QStringLiteral("Sonde Predict"));
    writer.setPageSize(QPageSize(QPageSize::A4));
    // print() adds 2 cm margins of its own and lays the text out at screen resolution (96 dpi):
    // that leaves an A4 text column about 640 px wide, so keep pictures narrower than that.
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    help::prepareDocument(document, 620);
    document->print(&writer);
    return {};
}

void HelpWindow::saveAsPdf()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save the manual as PDF"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Sonde Predict manual.pdf"),
        tr("PDF files (*.pdf)"));
    if (path.isEmpty())
        return;
    if (const QString error = writePdf(path); !error.isEmpty())
        QMessageBox::warning(this, tr("Save as PDF"), error);
}
