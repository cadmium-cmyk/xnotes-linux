#include "Icons.h"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace {

class PaletteIconEngine : public QIconEngine {
public:
    explicit PaletteIconEngine(QString name) : name_(std::move(name)) {}

    QIconEngine* clone() const override { return new PaletteIconEngine(name_); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        currentIcon().paint(painter, rect, Qt::AlignCenter, mode, state);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return currentIcon().pixmap(size, mode, state);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        return currentIcon().pixmap(size, scale, mode, state);
    }

    QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return currentIcon().actualSize(size, mode, state);
    }

    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override {
        return currentIcon().availableSizes(mode, state);
    }

private:
    QIcon currentIcon() const {
        return xnIcon(name_, QApplication::palette().color(QPalette::ButtonText));
    }

    QString name_;
};

}  // namespace

QIcon xnIcon(const QString& name) {
    return QIcon(new PaletteIconEngine(name));
}

QIcon xnIcon(const QString& name, const QColor& color) {
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('@') + color.name(QColor::HexArgb);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    // The files are drawn in black; recolour by text, then rasterize per size so no size is blurry.
    QByteArray svg = file.readAll();
    svg.replace("#000000", color.name(QColor::HexRgb).toLatin1());
    QIcon icon;
    for (int px : {16, 20, 24, 32, 40, 48, 64, 72}) {
        QBuffer buffer(&svg);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "svg");
        reader.setScaledSize(QSize(px, px));
        const QImage image = reader.read();
        if (!image.isNull()) icon.addPixmap(QPixmap::fromImage(image));
    }
    cache.insert(key, icon);
    return icon;
}
