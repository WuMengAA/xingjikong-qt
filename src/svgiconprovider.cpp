#include "svgiconprovider.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <QColor>

SvgIconProvider::SvgIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap SvgIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // id 形如 "eye" 或 "eye?color=%23ffffff"（color 经 encodeURIComponent 编码）
    QString name = id;
    QColor color(Qt::white);
    const int q = id.indexOf('?');
    if (q >= 0) {
        QUrlQuery qr(id.mid(q + 1));
        name = id.left(q);
        if (qr.hasQueryItem("color"))
            color = QColor(qr.queryItemValue("color"));
    }

    const int s = requestedSize.width() > 0 ? requestedSize.width() : 20;

    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return QPixmap();
    const QByteArray raw = f.readAll();
    f.close();

    QString svg = QString::fromUtf8(raw);
    svg.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
    QSvgRenderer r;
    if (!r.load(svg.toUtf8()))
        return QPixmap();

    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p, QRect(0, 0, s, s));
    if (size)
        *size = QSize(s, s);
    return pm;
}
