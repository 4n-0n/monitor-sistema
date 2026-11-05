#pragma once

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QRectF>

namespace theme {

// fondo blu notte, ciano di segnale, verde ok, ambra warn, rosso danger
inline const QColor bg0(0x06, 0x0a, 0x11);
inline const QColor bg1(0x0b, 0x13, 0x21);
inline const QColor panel0(0x11, 0x1c, 0x2e);
inline const QColor panel1(0x0a, 0x11, 0x1d);
inline const QColor panelFill(0x0f, 0x19, 0x2a);   // riempimento piatto dei pannelli
inline const QColor line(0x1d, 0x2c, 0x42);
inline const QColor lineSoft(0x16, 0x22, 0x34);
inline const QColor accent(0x38, 0xd6, 0xff);
inline const QColor ok(0x45, 0xe0, 0xa0);
inline const QColor warn(0xff, 0xb3, 0x47);
inline const QColor danger(0xff, 0x54, 0x67);
inline const QColor text(0xe8, 0xf3, 0xff);
inline const QColor dim(0x82, 0x96, 0xb0);
inline const QColor faint(0x4a, 0x5b, 0x73);

QString monoFamily();
QFont mono(double px, bool bold = false, double spacing = 0.0);
QColor heat(double pct, double warnAt = 80.0, double dangerAt = 92.0);
QLinearGradient bgGradient(const QRectF &r);

} // namespace theme
