#include "ImageValueView.h"

#include "ImageValue.h"

#include <QCache>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace imagevalues {
namespace {
const char* formatOf(const std::string& mediaType) {
    if (mediaType == "image/png") return "PNG";
    if (mediaType == "image/jpeg") return "JPEG";
    if (mediaType == "image/gif") return "GIF";
    if (mediaType == "image/webp") return "WEBP";
    if (mediaType == "image/bmp") return "BMP";
    return nullptr;
}
} // namespace

QImage load(const QString& value, QString* error) {
    const auto image = lexicon::parseImageValue(qtbridge::toCore(value));
    if (!image) {
        if (error) *error = "This is not an image value.";
        return {};
    }
    auto bytes = services().core.blobs.readData(image->hash);
    if (!bytes) {
        if (error) *error = qtbridge::toQt(bytes.error().message);
        return {};
    }
    QImage picture = QImage::fromData(QByteArray::fromStdString(*bytes), formatOf(image->mediaType));
    if (picture.isNull() && error) *error = "Qt cannot read this " + qtbridge::toQt(image->mediaType) + " image.";
    return picture;
}

QPixmap thumbnail(const QString& value, int edge) {
    static QCache<QString, QPixmap> cache(64);
    const QString key = QString::number(edge) + "/" + value;
    if (const QPixmap* known = cache.object(key)) return *known;
    const QImage image = load(value);
    if (image.isNull()) return {};
    auto* pixmap = new QPixmap(QPixmap::fromImage(
        image.width() > edge || image.height() > edge
            ? image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image));
    const QPixmap result = *pixmap;
    cache.insert(key, pixmap);
    return result;
}

QString describe(const QString& value, const QImage& image) {
    const auto parsed = lexicon::parseImageValue(qtbridge::toCore(value));
    if (!parsed) return {};
    const QString kind = QString("%1 image").arg(formatOf(parsed->mediaType));
    return image.isNull() ? kind : QString("%1, %2 × %3").arg(kind).arg(image.width()).arg(image.height());
}

QString fileFilter() {
    return "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp);;All files (*)";
}

QString importFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Cannot open the file: " + file.errorString();
        return {};
    }
    const std::string type = lexicon::sniffImageType(file.read(16).toStdString());
    file.close();
    if (type.empty()) {
        if (error) *error = "Choose a PNG, JPEG, GIF, WebP or BMP image.";
        return {};
    }
    const QString hash = services().blobs.importFile(path, error);
    if (hash.isEmpty()) return {};
    return qtbridge::toQt(lexicon::formatImageValue(type, qtbridge::toCore(hash)));
}

QString suggestedFileName(const QString& name, const QString& value) {
    const auto parsed = lexicon::parseImageValue(qtbridge::toCore(value));
    QString base = name.trimmed();
    base.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    if (base.isEmpty()) base = "image";
    return parsed ? base + "." + qtbridge::toQt(lexicon::imageExtension(parsed->mediaType)) : base;
}
} // namespace imagevalues

ImageViewDialog::ImageViewDialog(const QImage& image, const QString& title, QWidget* parent)
    : QDialog(parent), m_image(image) {
    setWindowTitle(title);
    resize(qBound(360, image.width() + 60, 1100), qBound(280, image.height() + 110, 820));
    auto* root = new QVBoxLayout(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_picture = new QLabel(m_scroll);
    m_picture->setObjectName("imagePicture");
    m_picture->setAlignment(Qt::AlignCenter);
    m_scroll->setWidget(m_picture);
    m_scroll->setWidgetResizable(true);
    root->addWidget(m_scroll, 1);
    auto* bottom = new QHBoxLayout();
    auto* fit = new QCheckBox("Fit to window", this);
    fit->setObjectName("imageFit");
    fit->setChecked(true);
    connect(fit, &QCheckBox::toggled, this, &ImageViewDialog::setFitsWindow);
    bottom->addWidget(fit);
    auto* size = new QLabel(QString("%1 × %2 pixels").arg(image.width()).arg(image.height()), this);
    size->setEnabled(false);
    bottom->addWidget(size);
    bottom->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottom->addWidget(buttons);
    root->addLayout(bottom);
    updatePicture();
}

void ImageViewDialog::setFitsWindow(bool fit) {
    m_fit = fit;
    updatePicture();
}

void ImageViewDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    updatePicture();
}

void ImageViewDialog::updatePicture() {
    const QSize room = m_scroll->viewport()->size() - QSize(4, 4);
    // Fitting only ever shrinks: a small image is shown at its own size.
    const bool shrink = m_fit && (m_image.width() > room.width() || m_image.height() > room.height());
    m_picture->setPixmap(QPixmap::fromImage(
        shrink ? m_image.scaled(room, Qt::KeepAspectRatio, Qt::SmoothTransformation) : m_image));
}
