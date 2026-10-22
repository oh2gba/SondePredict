#pragma once

#include <QDialog>

class QTextBrowser;
class QTextDocument;

// The user manual (docs/manual/user-manual.md, built into the app) in a window of its own.
class HelpWindow : public QDialog {
    Q_OBJECT
public:
    explicit HelpWindow(QWidget *parent = nullptr);

    // Writes the manual as an A4 PDF; returns an error message or an empty string.
    static QString writePdf(const QString &path);

private:
    void saveAsPdf();

    QTextBrowser *m_browser = nullptr;
};

namespace help {

inline constexpr const char *kManualUrl = "qrc:/manual/user-manual.md";

// Gives headings GitHub-style anchors ("Making a prediction" → "making-a-prediction") so the
// manual's own links work, and shrinks screenshots wider than `maxImageWidth`.
void prepareDocument(QTextDocument *document, int maxImageWidth);

} // namespace help
