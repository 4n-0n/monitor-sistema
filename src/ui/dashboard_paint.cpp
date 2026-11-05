#include "dashboard.h"

#include "core/format.h"
#include "theme.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSysInfo>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

namespace theme {

QString monoFamily()
{
    static QString family;
    if (!family.isEmpty())
        return family;
    const QStringList prefer{QStringLiteral("IBM Plex Mono"),   QStringLiteral("JetBrains Mono"),
                             QStringLiteral("Fira Code"),       QStringLiteral("DejaVu Sans Mono"),
                             QStringLiteral("Noto Sans Mono"),  QStringLiteral("Liberation Mono")};
    const QStringList have = QFontDatabase::families();
    for (const QString &p : prefer) {
        if (have.contains(p)) {
            family = p;
            break;
        }
    }
    if (family.isEmpty())
        family = QStringLiteral("monospace");
    return family;
}

QFont mono(double px, bool bold, double spacing)
{
    // cache: una QFont per ogni combinazione usata, non una per chiamata
    static QHash<QString, QFont> cache;
    const QString key = QStringLiteral("%1|%2|%3").arg(px).arg(bold).arg(spacing);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QFont f(monoFamily());
    f.setPixelSize(std::max(6, int(std::lround(px))));
    f.setBold(bold);
    if (spacing != 0.0)
        f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    cache.insert(key, f);
    return f;
}

QColor heat(double pct, double warnAt, double dangerAt)
{
    if (pct >= dangerAt)
        return danger;
    if (pct >= warnAt)
        return warn;
    return accent;
}

QLinearGradient bgGradient(const QRectF &r)
{
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, QColor(0x0b, 0x13, 0x21));
    g.setColorAt(0.5, bg0);
    g.setColorAt(1.0, QColor(0x08, 0x0e, 0x18));
    return g;
}

} // namespace theme

namespace {

constexpr qreal kHead = 30.0;

void text(QPainter &p, const QRectF &r, int flags, const QString &s, const QFont &f, const QColor &c)
{
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, flags, s);
}

// testo che non sborda mai: si taglia con i puntini invece di uscire dal pannello
void elided(QPainter &p, const QRectF &r, const QString &s, const QFont &f, const QColor &c)
{
    const QFontMetricsF fm(f);
    text(p, r, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(s, Qt::ElideRight, r.width()), f, c);
}

void panel(QPainter &p, const QRectF &r, const QString &title, const QColor &accentColor,
           const QString &right = QString())
{
    if (r.width() < 40 || r.height() < 30)
        return;

    // riempimento piatto: un gradiente per pannello a ogni frame costava più di
    // tutto il resto del disegno, a 5-25 Hz si vede la differenza sul processore
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(theme::panelFill);
    p.setPen(QPen(theme::line, 1));
    p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setPen(QPen(QColor(255, 255, 255, 16), 1));
    p.drawLine(QPointF(r.left() + 1, r.top() + 1.5), QPointF(r.right() - 1, r.top() + 1.5));
    p.setRenderHint(QPainter::Antialiasing, true);

    // angoli tecnici a L
    p.setPen(QPen(accentColor, 1.7));
    const qreal L = 9.0;
    const QRectF c = r.adjusted(1.0, 1.0, -1.0, -1.0);
    p.drawLine(QPointF(c.left(), c.top() + L), QPointF(c.left(), c.top() + 1));
    p.drawLine(QPointF(c.left() + 1, c.top()), QPointF(c.left() + L, c.top()));
    p.drawLine(QPointF(c.right() - L, c.top()), QPointF(c.right() - 1, c.top()));
    p.drawLine(QPointF(c.right(), c.top() + 1), QPointF(c.right(), c.top() + L));
    p.drawLine(QPointF(c.left(), c.bottom() - L), QPointF(c.left(), c.bottom() - 1));
    p.drawLine(QPointF(c.left() + 1, c.bottom()), QPointF(c.left() + L, c.bottom()));
    p.drawLine(QPointF(c.right() - L, c.bottom()), QPointF(c.right() - 1, c.bottom()));
    p.drawLine(QPointF(c.right(), c.bottom() - 1), QPointF(c.right(), c.bottom() - L));

    text(p, QRectF(r.left() + 14, r.top() + 8, r.width() - 150, 16),
         Qt::AlignLeft | Qt::AlignVCenter, title, theme::mono(9.5, true, 1.3), accentColor);
    if (!right.isEmpty())
        text(p, QRectF(r.right() - 160, r.top() + 8, 146, 16),
             Qt::AlignRight | Qt::AlignVCenter, right, theme::mono(9.0, false, 0.6), theme::faint);

    p.setPen(QPen(theme::lineSoft, 1));
    const qreal y = r.top() + kHead - 3;
    p.drawLine(QPointF(r.left() + 12, y), QPointF(r.right() - 12, y));
}

void chip(QPainter &p, const QRectF &r, const QString &label, const QString &value,
          const QColor &valueColor)
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(theme::line, 1));
    p.setBrush(QColor(255, 255, 255, 8));
    p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setRenderHint(QPainter::Antialiasing, true);
    text(p, QRectF(r.left() + 9, r.top() + 3, r.width() - 14, 12), Qt::AlignLeft | Qt::AlignVCenter,
         label, theme::mono(8.0, true, 1.2), theme::faint);
    text(p, QRectF(r.left() + 9, r.top() + 14, r.width() - 14, 16), Qt::AlignLeft | Qt::AlignVCenter,
         value, theme::mono(12.0, true, 0.4), valueColor);
}

void hbar(QPainter &p, const QRectF &r, double frac, const QColor &c)
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x13, 0x1f, 0x31));
    p.drawRect(r);
    const double f = std::clamp(frac, 0.0, 1.0);
    if (f > 0.0) {
        p.setBrush(c);
        p.drawRect(QRectF(r.left(), r.top(), std::max(2.0, r.width() * f), r.height()));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
}

void spark(QPainter &p, const QRectF &r, const QVector<double> &v, const QColor &c, double lo,
           double hi, int capacity = 0)
{
    if (r.width() < 4 || r.height() < 4)
        return;

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(theme::lineSoft, 1));
    for (int i = 1; i < 4; ++i) {
        const qreal y = r.top() + r.height() * i / 4.0;
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    if (v.size() < 2)
        return;
    if (hi - lo <= 0.0) {
        hi = lo + 1.0;
    }

    // asse del tempo ancorato a destra: con pochi campioni la linea resta corta
    // invece di occupare tutta la larghezza come se la cronologia fosse piena
    const int cap = std::max(2, capacity > 0 ? capacity : int(v.size()));
    const qreal slot = r.width() / double(cap - 1);
    const int n = v.size();
    const auto xFor = [&](int i) { return r.right() - double(n - 1 - i) * slot; };
    const auto yFor = [&](double val) {
        return r.bottom() - std::clamp((val - lo) / (hi - lo), 0.0, 1.0) * r.height();
    };

    QPolygonF poly;
    const int maxPts = std::max(16, int(r.width() * 2.0));
    if (n <= maxPts) {
        poly.reserve(n);
        for (int i = 0; i < n; ++i)
            poly << QPointF(xFor(i), yFor(v.at(i)));
    } else {
        // 25 Hz su 3 minuti = 4500 punti: si disegnano per colonna di pixel
        // tenendo min e max del gruppo, così i picchi non spariscono
        const int bucket = (n + (maxPts / 2) - 1) / (maxPts / 2);
        for (int start = 0; start < n; start += bucket) {
            const int end = std::min(n, start + bucket);
            int iMin = start, iMax = start;
            for (int i = start + 1; i < end; ++i) {
                if (v.at(i) < v.at(iMin))
                    iMin = i;
                if (v.at(i) > v.at(iMax))
                    iMax = i;
            }
            const int first = std::min(iMin, iMax);
            const int second = std::max(iMin, iMax);
            poly << QPointF(xFor(first), yFor(v.at(first)));
            if (second != first)
                poly << QPointF(xFor(second), yFor(v.at(second)));
        }
    }
    if (poly.size() < 2)
        return;

    QPainterPath area;
    area.moveTo(poly.first().x(), r.bottom());
    area.lineTo(poly.first());
    for (int i = 1; i < poly.size(); ++i)
        area.lineTo(poly.at(i));
    area.lineTo(poly.last().x(), r.bottom());
    area.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(c.red(), c.green(), c.blue(), 38));
    p.drawPath(area);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly);

    // punto corrente
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(poly.last(), 2.6, 2.6);
}

void sparkAuto(QPainter &p, const QRectF &r, const QVector<double> &v, const QColor &c,
               int capacity = 0)
{
    if (v.isEmpty())
        return;
    double lo = v.first();
    double hi = v.first();
    for (double x : v) {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    if (hi - lo < 2.0) {
        const double mid = (hi + lo) / 2.0;
        lo = mid - 1.0;
        hi = mid + 1.0;
    }
    const double pad = (hi - lo) * 0.2;
    spark(p, r, v, c, lo - pad, hi + pad, capacity);
}

void toggle(QPainter &p, const QRectF &r, const QString &label, bool on)
{
    p.setPen(QPen(on ? theme::accent : theme::line, 1));
    p.setBrush(on ? QColor(0x10, 0x33, 0x46) : QColor(0x0d, 0x16, 0x25));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.setPen(Qt::NoPen);
    p.setBrush(on ? theme::accent : theme::faint);
    p.drawEllipse(QRectF(r.left() + 7, r.center().y() - 4.5, 9, 9));
    text(p, r.adjusted(22, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, label,
         theme::mono(9.5, true, 1.0), on ? theme::text : theme::dim);
}

// soglie tarate su questa macchina: vive a 96 °C, allarmare a 80 sarebbe rumore
QColor tempColor(double celsius)
{
    if (celsius >= 99.0)
        return theme::danger;
    if (celsius >= 96.0)
        return theme::warn;
    return theme::ok;
}

QString joinedNames(const QVector<TempSample> &temps, int maxItems)
{
    QStringList parts;
    for (int i = 0; i < temps.size() && i < maxItems; ++i) {
        const TempSample &t = temps.at(i);
        QString name = t.label;
        const int dot = name.indexOf(QStringLiteral(" · "));
        if (dot > 0)
            name = name.mid(dot + 3);
        parts << QStringLiteral("%1 %2°").arg(name, fmt::num(t.celsius, 0));
    }
    return parts.join(QStringLiteral("   "));
}

} // namespace

void Dashboard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // MON_PAINT_BENCH=1 stampa il costo medio del disegno (diagnostica, non UI)
    static const bool bench = qEnvironmentVariableIsSet("MON_PAINT_BENCH");
    QElapsedTimer benchTimer;
    if (bench)
        benchTimer.start();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // fondo (gradiente + griglia) calcolato una volta per dimensione, non a ogni frame
    if (m_bg.isNull())
        rebuildBackground();
    p.drawPixmap(0, 0, m_bg);

    buildLayout();
    if (!m_layout.valid)
        return;

    paintHeader(p);
    paintCpu(p, m_layout.cpu);
    paintMem(p, m_layout.mem);
    paintTemp(p, m_layout.temp);
    paintNet(p, m_layout.net);
    paintDisk(p, m_layout.disk);
    paintProcs(p, m_layout.procs);
    paintFooter(p);

    if (bench) {
        static qint64 total = 0;
        static int frames = 0;
        static QElapsedTimer wall;      // tempo reale dall'avvio, non somma dei paint
        if (!wall.isValid())
            wall.start();
        total += benchTimer.nsecsElapsed();
        if (++frames % 50 == 0) {
            const double secs = std::max(0.001, double(wall.elapsed()) / 1000.0);
            qInfo("paint: %.2f ms/frame, %d frame, %.1f campioni/s",
                  double(total) / 1e6 / frames, frames, double(m_samples) / secs);
        }
    }
}

void Dashboard::paintHeader(QPainter &p)
{
    const QRectF r = m_layout.header;
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0.0, QColor(0x11, 0x1c, 0x2e));
    g.setColorAt(1.0, QColor(0x0a, 0x12, 0x1e));
    p.setBrush(g);
    p.setPen(QPen(theme::line, 1));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

    p.setPen(QPen(theme::accent, 2.0));
    p.drawLine(QPointF(r.left() + 1, r.top() + 14), QPointF(r.left() + 1, r.bottom() - 14));

    text(p, QRectF(r.left() + 18, r.top() + 11, 380, 24), Qt::AlignLeft | Qt::AlignVCenter,
         QStringLiteral("MONITOR DI SISTEMA"), theme::mono(16.0, true, 2.4), theme::text);

    QString host = m_machine.host;
    if (host.isEmpty())
        host = QSysInfo::machineHostName();
    const QString kernel = m_machine.kernel.isEmpty() ? QSysInfo::kernelVersion() : m_machine.kernel;
    QString sub = QStringLiteral("%1  ·  kernel %2").arg(host, kernel);
    if (m_machine.uptimeSec > 0)
        sub += QStringLiteral("  ·  acceso da %1").arg(fmt::duration(m_machine.uptimeSec));
    text(p, QRectF(r.left() + 18, r.top() + 40, r.width() * 0.55, 18),
         Qt::AlignLeft | Qt::AlignVCenter, sub, theme::mono(10.0, false, 0.5), theme::dim);

    // orologio a destra
    const QRectF clockRect(r.right() - 158, r.top() + 10, 142, 54);
    const QDateTime now = QDateTime::currentDateTime();
    text(p, QRectF(clockRect.left(), clockRect.top(), clockRect.width(), 30),
         Qt::AlignRight | Qt::AlignVCenter, now.toString(QStringLiteral("HH:mm:ss")),
         theme::mono(24.0, true, 1.0), theme::text);
    static const char *months[] = {"gen", "feb", "mar", "apr", "mag", "giu",
                                   "lug", "ago", "set", "ott", "nov", "dic"};
    static const char *days[] = {"lun", "mar", "mer", "gio", "ven", "sab", "dom"};
    const QString dateStr = QStringLiteral("%1 %2 %3 %4")
                                .arg(QString::fromLatin1(days[now.date().dayOfWeek() - 1]),
                                     fmt::num(now.date().day(), 0),
                                     QString::fromLatin1(months[now.date().month() - 1]),
                                     fmt::num(now.date().year(), 0));
    text(p, QRectF(clockRect.left(), clockRect.top() + 30, clockRect.width(), 16),
         Qt::AlignRight | Qt::AlignVCenter, dateStr, theme::mono(9.0, false, 0.8), theme::faint);

    // chip di riepilogo, da destra verso sinistra
    const qreal chipsRight = clockRect.left() - 16;
    const qreal leftLimit = r.left() + 18 + 400;
    const qreal cw = 104.0;
    const qreal gap = 8.0;
    const int fit = int(std::max(0.0, chipsRight - leftLimit + gap) / (cw + gap));
    const int n = std::min(4, fit);
    const qreal total = n * cw + (n - 1) * gap;
    qreal x = chipsRight - total;
    const qreal y = r.center().y() - 17;

    const double cpu = std::max(0.0, m_machine.cpuPct);
    const double mem = m_machine.memUsedPct();
    const double temp = m_machine.maxTemp();
    const QString labels[4] = {QStringLiteral("CPU"), QStringLiteral("MEMORIA"),
                               QStringLiteral("TEMPERATURA"), QStringLiteral("PROCESSI")};
    const QString values[4] = {
        m_machine.valid ? fmt::pct(cpu) : QStringLiteral("—"),
        m_machine.valid ? fmt::pct(mem) : QStringLiteral("—"),
        temp > 0 ? fmt::num(temp, 0) + QStringLiteral(" °C") : QStringLiteral("—"),
        m_machine.procsTotal > 0 ? fmt::num(m_machine.procsRunning, 0) : QStringLiteral("—"),
    };
    const QColor colors[4] = {theme::heat(cpu), theme::heat(mem),
                              temp > 0 ? tempColor(temp) : theme::faint, theme::accent};
    for (int i = 0; i < n; ++i) {
        chip(p, QRectF(x, y, cw, 34), labels[i], values[i], colors[i]);
        x += cw + gap;
    }
}

void Dashboard::paintCpu(QPainter &p, const QRectF &r)
{
    const int nCores = m_machine.cores.size();
    panel(p, r, QStringLiteral("CARICO PROCESSORE"), theme::accent,
          nCores > 0 ? QStringLiteral("%1 CORE  ·  %2").arg(nCores).arg(fmt::mhz(m_machine.freqMhz))
                     : QString());

    const QRectF in = r.adjusted(14, kHead + 8, -14, -12);
    if (in.width() < 100 || in.height() < 40)
        return;

    const qreal leftW = std::min(190.0, in.width() * 0.36);
    const QRectF bigBlock(in.left(), in.top(), leftW, in.height());

    text(p, QRectF(bigBlock.left(), bigBlock.top(), bigBlock.width(), 14),
         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("CARICO"),
         theme::mono(8.5, true, 1.4), theme::faint);

    const double cpu = m_machine.valid ? std::max(0.0, m_machine.cpuPct) : -1.0;
    const QString value = cpu < 0 ? QStringLiteral("—") : fmt::num(cpu, 1);
    const QFont bigFont = theme::mono(42.0, true, 0.0);
    const QFontMetricsF bigFm(bigFont);
    const qreal valueW = bigFm.horizontalAdvance(value);
    text(p, QRectF(bigBlock.left(), bigBlock.top() + 12, bigBlock.width(), 50),
         Qt::AlignLeft | Qt::AlignVCenter, value, bigFont,
         cpu < 0 ? theme::dim : theme::heat(cpu));
    text(p, QRectF(bigBlock.left() + valueW + 2, bigBlock.top() + 26, 30, 24),
         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%"), theme::mono(15.0, true),
         theme::dim);

    const qreal lineH = 15.5;
    qreal y = bigBlock.top() + 64;
    if (y + lineH <= in.bottom()) {
        elided(p, QRectF(bigBlock.left(), y, bigBlock.width(), lineH),
               QStringLiteral("carico  %1  %2  %3")
                   .arg(fmt::num(m_machine.load1, 2), fmt::num(m_machine.load5, 2),
                        fmt::num(m_machine.load15, 2)),
               theme::mono(9.0, false, 0.2), theme::dim);
        y += lineH;
    }

    QString detail;
    if (m_machine.pressureCpu > 0.0)
        detail = QStringLiteral("press. %1%").arg(fmt::num(m_machine.pressureCpu, 1));
    if (m_machine.batteryPresent) {
        if (!detail.isEmpty())
            detail += QStringLiteral("  ·  ");
        detail += QStringLiteral("bat. %1%%2").arg(
            fmt::num(m_machine.batteryPct, 1),
            m_machine.charging ? QStringLiteral(" carica") : QString());
    }
    if (!detail.isEmpty() && y + lineH <= in.bottom()) {
        elided(p, QRectF(bigBlock.left(), y, bigBlock.width(), lineH), detail,
               theme::mono(9.0, false, 0.2),
               m_machine.batteryPct < 20 && !m_machine.charging && m_machine.batteryPresent
                   ? theme::danger
                   : theme::faint);
    }

    const QRectF bars(in.left() + leftW + 8, in.top(), in.width() - leftW - 8, in.height());
    if (nCores <= 0) {
        text(p, bars, Qt::AlignCenter, QStringLiteral("in attesa del primo campione…"),
             theme::mono(10.0), theme::faint);
        return;
    }

    const qreal baseline = bars.bottom() - 15;
    const qreal top = bars.top() + 8;
    const qreal h = std::max(4.0, baseline - top);
    const qreal slot = bars.width() / nCores;
    const qreal barW = std::clamp(slot - 4.0, 2.0, 18.0);

    p.setPen(QPen(QColor(0x27, 0x3a, 0x54, 120), 1, Qt::DashLine));
    p.drawLine(QPointF(bars.left(), baseline - h * 0.5), QPointF(bars.right(), baseline - h * 0.5));

    const bool showEvery = nCores <= 12;
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int i = 0; i < nCores; ++i) {
        const double pct = std::clamp(m_machine.cores.at(i).pct, 0.0, 100.0);
        const qreal cx = bars.left() + slot * (i + 0.5);
        const QRectF track(cx - barW / 2.0, top, barW, h);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x14, 0x20, 0x33));
        p.drawRect(track);
        if (pct > 0.4) {
            const qreal fh = h * pct / 100.0;
            const QRectF fill(track.left(), baseline - fh, barW, fh);
            const QColor c = theme::heat(pct);
            p.setBrush(c);
            p.drawRect(fill);
            // filo più chiaro in cima: dà profondità senza un gradiente per barra
            p.setBrush(c.lighter(135));
            p.drawRect(QRectF(fill.left(), fill.top(), fill.width(), std::min<qreal>(2.0, fh)));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::NoBrush);
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < nCores && showEvery; ++i) {
        const qreal cx = bars.left() + slot * (i + 0.5);
        text(p, QRectF(cx - slot / 2.0, baseline + 2, slot, 12), Qt::AlignCenter,
             QStringLiteral("%1").arg(i), theme::mono(7.5, false, 0.2), theme::faint);
    }

    p.setPen(QPen(theme::line, 1));
    p.drawLine(QPointF(bars.left(), baseline), QPointF(bars.right(), baseline));
}

void Dashboard::paintMem(QPainter &p, const QRectF &r)
{
    panel(p, r, QStringLiteral("MEMORIA"), theme::accent);
    const QRectF in = r.adjusted(14, kHead + 10, -14, -14);
    if (in.width() < 160 || in.height() < 60)
        return;

    const double pctUsed = m_machine.memUsedPct();
    const qreal side = std::min({in.height(), 120.0, in.width() * 0.4});
    const QRectF donutR(in.left(), in.top() + (in.height() - side) / 2.0, side, side);

    const QRectF ring = donutR.adjusted(7, 7, -7, -7);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x15, 0x22, 0x35), 9, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(ring, 90 * 16, -360 * 16);
    if (m_machine.valid) {
        p.setPen(QPen(theme::heat(pctUsed), 9, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ring, 90 * 16, -int(360 * 16 * std::clamp(pctUsed / 100.0, 0.0, 1.0)));
    }
    text(p, QRectF(donutR.left(), donutR.center().y() - 16, donutR.width(), 22),
         Qt::AlignCenter, fmt::num(pctUsed, 0), theme::mono(21.0, true, 0.0),
         m_machine.valid ? theme::heat(pctUsed) : theme::dim);
    text(p, QRectF(donutR.left(), donutR.center().y() + 6, donutR.width(), 14),
         Qt::AlignCenter, QStringLiteral("USATA"), theme::mono(7.5, true, 1.4), theme::faint);

    const QRectF rows(donutR.right() + 18, in.top() + 2, in.width() - donutR.width() - 22,
                      in.height() - 4);
    if (rows.width() < 80)
        return;

    int rowCount = 0;
    if (m_machine.memTotal > 0)
        ++rowCount;
    if (m_machine.swapTotal > 0)
        ++rowCount;
    if (m_machine.batteryPresent)
        ++rowCount;
    if (rowCount == 0)
        return;

    // le righe si distribuiscono nello spazio: a finestra piccola non sbordano
    const qreal avail = std::max(0.0, rows.height() - 6.0);
    const qreal rowH = std::clamp(avail / rowCount, 30.0, 48.0);
    qreal y = rows.top() + std::max(0.0, (avail - rowCount * rowH) / 2.0);
    const qreal barY = std::min(rowH * 0.45, rowH - 12.0);
    const QFont labelFont = theme::mono(8.5, true, 1.4);
    const QFont valueFont = theme::mono(10.5, true, 0.2);
    const QFontMetricsF labelFm(labelFont);
    const QFontMetricsF valueFm(valueFont);
    const auto metricRow = [&](const QString &label, const QString &value, double frac,
                               const QColor &color) {
        text(p, QRectF(rows.left(), y, rows.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, label,
             labelFont, theme::faint);
        const qreal room = rows.width() - labelFm.horizontalAdvance(label) - 8.0;
        text(p, QRectF(rows.left(), y, rows.width(), 14), Qt::AlignRight | Qt::AlignVCenter,
             valueFm.elidedText(value, Qt::ElideRight, std::max(20.0, room)), valueFont,
             theme::text);
        hbar(p, QRectF(rows.left(), y + barY, rows.width(), 6), frac, color);
        y += rowH;
    };

    if (m_machine.memTotal > 0) {
        metricRow(QStringLiteral("RAM"),
                  QStringLiteral("%1 / %2").arg(fmt::bytes(m_machine.memTotal - m_machine.memAvail),
                                                fmt::bytes(m_machine.memTotal)),
                  pctUsed / 100.0, theme::heat(pctUsed));
    }
    if (m_machine.swapTotal > 0) {
        const double sp = m_machine.swapUsedPct();
        metricRow(QStringLiteral("SWAP"),
                  QStringLiteral("%1 / %2").arg(fmt::bytes(m_machine.swapTotal - m_machine.swapFree),
                                                fmt::bytes(m_machine.swapTotal)),
                  sp / 100.0, theme::heat(sp, 60, 85));
    }
    if (m_machine.batteryPresent) {
        const QString val = m_machine.charging
                                ? QStringLiteral("%1 · in carica").arg(fmt::pct(m_machine.batteryPct))
                                : QStringLiteral("%1 · a batteria")
                                      .arg(fmt::pct(m_machine.batteryPct));
        metricRow(QStringLiteral("BATTERIA"), val, m_machine.batteryPct / 100.0,
                  m_machine.batteryPct < 20 && !m_machine.charging ? theme::danger : theme::ok);
    }
}

void Dashboard::paintTemp(QPainter &p, const QRectF &r)
{
    const double tmax = m_machine.maxTemp();
    const int windowSec = (m_capacity * m_intervalMs) / 1000;
    const QString windowLabel =
        windowSec >= 120 ? QStringLiteral("ULTIMI %1 MIN").arg(windowSec / 60)
                         : QStringLiteral("ULTIMI %1 S").arg(windowSec);
    panel(p, r, QStringLiteral("TEMPERATURE"), tempColor(tmax),
          m_demo ? QStringLiteral("DEMO") : windowLabel);

    const QRectF in = r.adjusted(14, kHead + 6, -14, -10);
    if (in.width() < 60 || in.height() < 40)
        return;

    text(p, QRectF(in.left(), in.top(), 90, 26), Qt::AlignLeft | Qt::AlignVCenter,
         tmax > 0 ? fmt::num(tmax, 0) : QStringLiteral("—"), theme::mono(22.0, true, 0.0),
         tmax > 0 ? tempColor(tmax) : theme::dim);
    if (tmax > 0) {
        const QFontMetricsF fm(theme::mono(22.0, true, 0.0));
        text(p, QRectF(in.left() + fm.horizontalAdvance(fmt::num(tmax, 0)) + 3, in.top() + 5, 26, 18),
             Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("°C"), theme::mono(11.0, true),
             theme::dim);
    }
    if (m_tempHist.v.size() > 1) {
        double lo = m_tempHist.v.first();
        double hi = lo;
        for (double v : m_tempHist.v) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        text(p, QRectF(in.right() - 92, in.top() + 4, 92, 14), Qt::AlignRight | Qt::AlignVCenter,
             QStringLiteral("%1 – %2 °C").arg(fmt::num(lo, 0), fmt::num(hi, 0)),
             theme::mono(8.5, false, 0.3), theme::faint);
    }

    const QRectF graph(in.left(), in.top() + 28, in.width(), std::max(10.0, in.height() - 46));
    sparkAuto(p, graph, m_tempHist.v, tempColor(tmax), m_capacity);

    text(p, QRectF(in.left(), r.bottom() - 24, in.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
         joinedNames(m_machine.temps, 3), theme::mono(8.5, false, 0.2), theme::dim);
}

void Dashboard::paintNet(QPainter &p, const QRectF &r)
{
    double peak = 1.0;
    for (double v : m_netHist.v)
        peak = std::max(peak, v);
    panel(p, r, QStringLiteral("RETE"), theme::accent,
          QStringLiteral("PICCO %1").arg(fmt::rate(peak)));
    const QRectF in = r.adjusted(14, kHead + 6, -14, -10);
    if (in.width() < 60 || in.height() < 40)
        return;

    const qreal rowH = 26.0;
    text(p, QRectF(in.left(), in.top(), 16, rowH), Qt::AlignLeft | Qt::AlignVCenter,
         QStringLiteral("↓"), theme::mono(15.0, true), theme::ok);
    text(p, QRectF(in.left() + 18, in.top(), in.width() - 18, rowH),
         Qt::AlignLeft | Qt::AlignVCenter, fmt::rate(m_machine.net.rxBps),
         theme::mono(14.0, true, 0.3), theme::text);
    text(p, QRectF(in.left(), in.top() + rowH, 16, rowH), Qt::AlignLeft | Qt::AlignVCenter,
         QStringLiteral("↑"), theme::mono(15.0, true), theme::accent);
    text(p, QRectF(in.left() + 18, in.top() + rowH, in.width() - 18, rowH),
         Qt::AlignLeft | Qt::AlignVCenter, fmt::rate(m_machine.net.txBps),
         theme::mono(14.0, true, 0.3), theme::text);

    const QRectF graph(in.left(), in.top() + 2 * rowH + 4, in.width(),
                       std::max(8.0, in.height() - 2 * rowH - 6));
    spark(p, graph, m_netHist.v, theme::ok, 0.0, peak * 1.15, m_capacity);
}

void Dashboard::paintDisk(QPainter &p, const QRectF &r)
{
    double peak = 1.0;
    for (double v : m_diskHist.v)
        peak = std::max(peak, v);
    panel(p, r, QStringLiteral("DISCO"), theme::accent,
          QStringLiteral("PICCO %1").arg(fmt::rate(peak)));
    const QRectF in = r.adjusted(14, kHead + 6, -14, -10);
    if (in.width() < 60 || in.height() < 40)
        return;

    const qreal rowH = 26.0;
    const QString labels[2] = {QStringLiteral("L"), QStringLiteral("S")};
    const QColor colors[2] = {theme::accent, theme::warn};
    const double vals[2] = {m_machine.disk.readBps, m_machine.disk.writeBps};
    for (int i = 0; i < 2; ++i) {
        text(p, QRectF(in.left(), in.top() + i * rowH, 14, rowH), Qt::AlignLeft | Qt::AlignVCenter,
             labels[i], theme::mono(9.0, true, 0.6), colors[i]);
        text(p, QRectF(in.left() + 16, in.top() + i * rowH, in.width() - 16, rowH),
             Qt::AlignLeft | Qt::AlignVCenter, fmt::rate(vals[i]), theme::mono(14.0, true, 0.3),
             theme::text);
    }

    const QRectF graph(in.left(), in.top() + 2 * rowH + 4, in.width(),
                       std::max(8.0, in.height() - 2 * rowH - 6));
    spark(p, graph, m_diskHist.v, theme::warn, 0.0, peak * 1.15, m_capacity);
}

void Dashboard::paintProcs(QPainter &p, const QRectF &r)
{
    const int shown = std::max(0, std::min(m_layout.procRows, int(m_machine.procs.size())));
    panel(p, r, QStringLiteral("PROCESSI"), theme::accent,
          m_machine.procsTotal > 0
              ? QStringLiteral("%1 ATTIVI / %2 TOTALI  ·  ORDINA %3")
                    .arg(m_machine.procsRunning)
                    .arg(m_machine.procsTotal)
                    .arg(m_sortByMem ? QStringLiteral("MEM") : QStringLiteral("CPU"))
              : QString());

    const QRectF in = r.adjusted(14, kHead + 4, -14, -10);
    if (in.width() < 120 || in.height() < 20)
        return;

    // colonne calcolate sui font reali, non a occhio
    const QFontMetricsF fmName(theme::mono(10.5));
    const QFontMetricsF fmValue(theme::mono(10.0, true));
    const qreal colPid = 58.0;
    const qreal colCpu = std::max(56.0, fmValue.horizontalAdvance(QStringLiteral("100,0 %")) + 18);
    const qreal colMem = std::max(66.0, fmValue.horizontalAdvance(QStringLiteral("999,9 MB")) + 18);
    const qreal colName = std::max(40.0, in.width() - colPid - colCpu - colMem - 12);

    const qreal headH = 18.0;
    const qreal rowH = 21.0;
    // blocco centrato in verticale quando l'altezza avanza, come i pannelli sopra
    const qreal contentH = headH + shown * rowH;
    const qreal startY = in.top() + std::max(0.0, std::floor((in.height() - contentH) / 2.0));

    text(p, QRectF(in.left(), startY, colPid, headH), Qt::AlignLeft | Qt::AlignVCenter,
         QStringLiteral("PID"), theme::mono(8.0, true, 1.2), theme::faint);
    text(p, QRectF(in.left() + colPid, startY, colName, headH),
         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PROCESSO"), theme::mono(8.0, true, 1.2),
         theme::faint);
    text(p, QRectF(in.left() + colPid + colName, startY, colCpu, headH),
         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("CPU"), theme::mono(8.0, true, 1.2),
         theme::faint);
    text(p, QRectF(in.left() + colPid + colName + colCpu, startY, colMem, headH),
         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("RAM"), theme::mono(8.0, true, 1.2),
         theme::faint);

    if (shown == 0) {
        text(p, QRectF(in.left(), startY + headH, in.width(), 22),
             Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("in attesa del primo campione…"),
             theme::mono(10.0), theme::faint);
        return;
    }

    QVector<ProcSample> procs = m_machine.procs;
    if (m_sortByMem) {
        std::sort(procs.begin(), procs.end(),
                  [](const ProcSample &a, const ProcSample &b) { return a.rss > b.rss; });
    }

    qreal y = startY + headH;
    for (int i = 0; i < shown; ++i) {
        const ProcSample &s = procs.at(i);
        const QRectF row(in.left(), y, in.width(), rowH);
        if (i % 2 == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 5));
            p.drawRect(row);
        }
        // barra della CPU in trasparenza dietro la riga
        const double frac = std::clamp(s.cpuPct / 100.0, 0.0, 1.0);
        if (frac > 0.01) {
            p.setPen(Qt::NoPen);
            QColor c = theme::heat(s.cpuPct, 60, 85);
            c.setAlpha(34);
            p.setBrush(c);
            p.drawRect(QRectF(row.left(), row.top(), row.width() * frac, row.height()));
        }

        text(p, QRectF(row.left(), row.top(), colPid, rowH), Qt::AlignLeft | Qt::AlignVCenter,
             fmt::num(s.pid, 0), theme::mono(9.5, false, 0.2), theme::faint);
        const QString name = fmName.elidedText(s.name, Qt::ElideRight, colName - 6);
        text(p, QRectF(row.left() + colPid, row.top(), colName, rowH),
             Qt::AlignLeft | Qt::AlignVCenter, name, theme::mono(10.5, false, 0.2), theme::text);
        text(p, QRectF(row.left() + colPid + colName, row.top(), colCpu, rowH),
             Qt::AlignRight | Qt::AlignVCenter, fmt::pct(s.cpuPct), theme::mono(10.0, true, 0.0),
             s.cpuPct >= 25 ? theme::warn : theme::ok);
        text(p, QRectF(row.left() + colPid + colName + colCpu, row.top(), colMem, rowH),
             Qt::AlignRight | Qt::AlignVCenter, fmt::bytes(s.rss), theme::mono(9.5, false, 0.0),
             theme::dim);
        y += rowH;
    }
}

void Dashboard::paintFooter(QPainter &p)
{
    const QRectF r = m_layout.footer;
    p.setPen(QPen(theme::lineSoft, 1));
    p.drawLine(QPointF(r.left(), r.top() + 1), QPointF(r.right(), r.top() + 1));

    const QString hint = m_paused
                             ? QStringLiteral("in pausa · SPAZIO riprende · F11 schermo intero · Q esci")
                             : QStringLiteral("SPAZIO pausa  ·  M ordina  ·  F11 schermo intero  ·  Q esci");
    text(p, QRectF(r.left() + 2, r.top(), r.width() - 320, r.height()),
         Qt::AlignLeft | Qt::AlignVCenter, hint, theme::mono(9.0, false, 0.6), theme::faint);

    const QString labels[ToggleCount] = {
        m_paused ? QStringLiteral("IN PAUSA") : QStringLiteral("LIVE"),
        m_sortByMem ? QStringLiteral("ORDINA MEM") : QStringLiteral("ORDINA CPU"),
        m_demo ? QStringLiteral("DEMO ON") : QStringLiteral("DEMO"),
    };
    const bool states[ToggleCount] = {m_paused, m_sortByMem, m_demo};
    for (int i = 0; i < m_layout.toggles.size() && i < ToggleCount; ++i)
        toggle(p, m_layout.toggles.at(i), labels[i], states[i]);
}
