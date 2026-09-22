#pragma once

#include "ApplicationContext.h"

#include <QDialog>
#include <QImage>
#include <QPixmap>

class QLabel;
class QScrollArea;

// Image values on the desktop: loading the stored file, choosing a new one,
// and a small picture for tables and the preview.
namespace imagevalues {
// Links to an image in the preview: lexicon-image:<Image value>.
inline constexpr char kScheme[] = "lexicon-image";
// The picture an Image value names, or a null image with the reason.
QImage load(const QString& value, QString* error = nullptr);
// At most edge x edge pixels, kept for values seen before.
QPixmap thumbnail(const QString& value, int edge);
// "PNG image, 640 × 480".
QString describe(const QString& value, const QImage& image = {});
QString fileFilter();
// Stores the image file at path and returns its Image value, or an empty
// string with the reason when it is no PNG, JPEG, GIF, WebP or BMP image.
QString importFile(const QString& path, QString* error);
// The stored file's name for Save as: "<name>.<extension>".
QString suggestedFileName(const QString& name, const QString& value);
} // namespace imagevalues

// One image at full size, scrolled when it is larger than the window.
class ImageViewDialog : public QDialog {
    Q_OBJECT

public:
    ImageViewDialog(const QImage& image, const QString& title, QWidget* parent = nullptr);
    bool fitsWindow() const { return m_fit; }
    void setFitsWindow(bool fit);

protected:
    void resizeEvent(QResizeEvent* event) override;
    // The picture is fitted to the scroll area's viewport whenever that
    // changes size - also when it is first laid out, after the dialog's own
    // resize, when a fit to the unsized viewport would show a thumbnail.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePicture();

    QImage m_image;
    QLabel* m_picture = nullptr;
    QScrollArea* m_scroll = nullptr;
    bool m_fit = true;
};
