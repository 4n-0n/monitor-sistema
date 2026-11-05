#pragma once

#include <QString>
#include <QtGlobal>

namespace fmt {

// numero con la virgola decimale italiana
QString num(double v, int decimals = 1);
QString bytes(quint64 b);          // 12,4 GB  (unità binarie, etichette decimali)
QString rate(double bps);          // 2,4 MB/s
QString duration(double seconds);  // 3g 04h 12m
QString pct(double v);             // 42,5%
QString mhz(double mhz);           // 3,82 GHz  /  842 MHz

} // namespace fmt
