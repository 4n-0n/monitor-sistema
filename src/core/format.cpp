#include "format.h"

#include <QStringList>
#include <cmath>

namespace fmt {

QString num(double v, int decimals)
{
    if (!std::isfinite(v))
        v = 0.0;
    QString s = QString::number(v, 'f', decimals);
    s.replace(QLatin1Char('.'), QLatin1Char(','));
    return s;
}

QString bytes(quint64 b)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = double(b);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    const int dec = (u == 0 || v >= 100.0) ? 0 : 1;
    return num(v, dec) + QLatin1Char(' ') + QLatin1String(units[u]);
}

QString rate(double bps)
{
    if (!std::isfinite(bps) || bps < 0.0)
        bps = 0.0;
    return bytes(quint64(bps + 0.5)) + QStringLiteral("/s");
}

QString duration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;
    const qint64 s = qint64(seconds);
    const qint64 d = s / 86400;
    const qint64 h = (s % 86400) / 3600;
    const qint64 m = (s % 3600) / 60;
    QStringList parts;
    if (d > 0)
        parts << QStringLiteral("%1g").arg(d);
    if (d > 0 || h > 0)
        parts << QStringLiteral("%1h").arg(h, 2, 10, QLatin1Char('0'));
    parts << QStringLiteral("%1m").arg(m, 2, 10, QLatin1Char('0'));
    return parts.join(QLatin1Char(' '));
}

QString pct(double v)
{
    return num(v, 1) + QStringLiteral(" %");
}

QString mhz(double mhzValue)
{
    if (!std::isfinite(mhzValue) || mhzValue <= 0.0)
        return QStringLiteral("—");
    if (mhzValue >= 1000.0)
        return num(mhzValue / 1000.0, 2) + QStringLiteral(" GHz");
    return num(mhzValue, 0) + QStringLiteral(" MHz");
}

} // namespace fmt
