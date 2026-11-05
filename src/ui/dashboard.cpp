#include "dashboard.h"

#include "core/format.h"
#include "theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kMargin = 14.0;
constexpr qreal kGap = 10.0;
constexpr qreal kHeaderH = 76.0;
constexpr qreal kFooterH = 44.0;
constexpr qreal kPanelHead = 30.0;
constexpr qreal kProcRowH = 21.0;
}

void Dashboard::Series::push(double x, int keep)
{
    v << x;
    while (v.size() > keep)
        v.removeFirst();
}

Dashboard::Dashboard(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Monitor di Sistema"));
    setMinimumSize(880, 560);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);

    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &Dashboard::onTick);
    m_timer->start();
    QTimer::singleShot(0, this, &Dashboard::onTick);
}

Dashboard::~Dashboard() = default;

void Dashboard::setDemo(bool on)
{
    if (m_demo == on)
        return;
    m_demo = on;
    if (on) {
        m_cpuHist.v.clear();
        m_memHist.v.clear();
        m_tempHist.v.clear();
        m_netHist.v.clear();
        m_diskHist.v.clear();
        m_demoFrame = 0;
        fillDemoHistory(m_capacity);
    }
    startTimerForMode();
    update();
}

void Dashboard::startTimerForMode()
{
    if (m_paused) {
        m_timer->stop();
        return;
    }
    m_timer->setInterval(m_intervalMs);
    m_timer->start();
}

void Dashboard::setIntervalMs(int ms)
{
    const int clamped = std::clamp(ms, 20, 10000);   // 50 Hz max, 0,1 Hz min
    if (clamped == m_intervalMs)
        return;
    m_intervalMs = clamped;
    applyCapacity();
    startTimerForMode();
    update();
}

void Dashboard::setWindowSec(int seconds)
{
    const int clamped = std::clamp(seconds, 5, 3600);
    if (clamped == m_windowSec)
        return;
    m_windowSec = clamped;
    applyCapacity();
    update();
}

void Dashboard::applyCapacity()
{
    m_capacity = std::max(30, (m_windowSec * 1000) / m_intervalMs);

    // le serie esistenti vengono riportate alla nuova capacità senza perdere l'ordine
    const auto trim = [this](Series &s) {
        while (s.v.size() > m_capacity)
            s.v.removeFirst();
    };
    trim(m_cpuHist);
    trim(m_memHist);
    trim(m_tempHist);
    trim(m_netHist);
    trim(m_diskHist);
}

void Dashboard::maybeRepaint()
{
    // campionare a 25 Hz non significa ridisegnare a 25 Hz: si dipinge al massimo
    // ogni m_paintIntervalMs, i campioni restano tutti nella cronologia
    if (m_paintClock.isValid() && m_paintClock.elapsed() < m_paintIntervalMs)
        return;
    m_paintClock.restart();
    update();
}

void Dashboard::rebuildBackground()
{
    if (width() <= 0 || height() <= 0)
        return;
    const qreal dpr = devicePixelRatioF();
    m_bg = QPixmap(QSize(int(std::ceil(width() * dpr)), int(std::ceil(height() * dpr))));
    m_bg.setDevicePixelRatio(dpr);

    QPainter bp(&m_bg);
    bp.setRenderHint(QPainter::Antialiasing, false);
    const QRectF full(0, 0, width(), height());
    bp.fillRect(full, theme::bgGradient(full));

    bp.setPen(QPen(QColor(0x2a, 0x3d, 0x58, 26), 1));
    for (qreal x = 0; x < width(); x += 48)
        bp.drawLine(QPointF(x, 0), QPointF(x, height()));
    for (qreal y = 0; y < height(); y += 48)
        bp.drawLine(QPointF(0, y), QPointF(width(), y));
}

void Dashboard::setPaused(bool on)
{
    if (m_paused == on)
        return;
    m_paused = on;
    startTimerForMode();
    update();
}

void Dashboard::setSortByMem(bool on)
{
    if (m_sortByMem == on)
        return;
    m_sortByMem = on;
    update();
}

void Dashboard::onTick()
{
    if (m_paused)
        return;
    if (m_demo) {
        ingest(demoFrame(++m_demoFrame));
        return;
    }
    ingest(m_telemetry.sample());
}

void Dashboard::ingest(const Machine &m)
{
    m_machine = m;
    m_cpuHist.push(std::max(0.0, m.cpuPct), m_capacity);
    m_memHist.push(m.memUsedPct(), m_capacity);
    m_tempHist.push(m.maxTemp(), m_capacity);
    m_netHist.push(m.net.rxBps + m.net.txBps, m_capacity);
    m_diskHist.push(m.disk.readBps + m.disk.writeBps, m_capacity);
    ++m_samples;
    maybeRepaint();
}

void Dashboard::fillDemoHistory(int frames)
{
    for (int i = frames; i > 0; --i) {
        Machine m = demoFrame(-i);
        m_cpuHist.push(std::max(0.0, m.cpuPct), m_capacity);
        m_memHist.push(m.memUsedPct(), m_capacity);
        m_tempHist.push(m.maxTemp(), m_capacity);
        m_netHist.push(m.net.rxBps + m.net.txBps, m_capacity);
        m_diskHist.push(m.disk.readBps + m.disk.writeBps, m_capacity);
    }
    ingest(demoFrame(0));
}

Machine Dashboard::demoFrame(int frame) const
{
    Machine m;
    m.valid = true;
    m.host = QStringLiteral("demo-local");
    m.kernel = QStringLiteral("6.10.0-demo");
    m.uptimeSec = 3 * 86400 + 4 * 3600 + 12 * 60 + frame;
    m.procsRunning = 342 + int(6 * std::sin(frame * 0.05));
    m.procsTotal = 2011;

    const double t = double(frame) * double(m_intervalMs) / 1000.0;
    m.cpuPct = std::clamp(26.0 + 17.0 * std::sin(t * 0.7) + 7.0 * std::sin(t * 3.3), 2.0, 99.0);
    for (int i = 0; i < 20; ++i) {
        CoreSample c;
        c.pct = std::clamp(m.cpuPct + 22.0 * std::sin(t * 1.3 + i * 0.9) + 8.0 * std::sin(t * 4.1 + i),
                           0.0, 100.0);
        c.mhz = 3400.0 + 400.0 * std::sin(t * 0.3 + i * 0.2);
        m.cores << c;
    }
    m.freqMhz = 3720.0 + 380.0 * std::sin(t * 0.35);
    m.load1 = m.cpuPct / 100.0 * m.cores.size() * 0.85;
    m.load5 = m.load1 * 0.92;
    m.load15 = m.load1 * 0.78;

    m.memTotal = 32773308416ULL;
    m.memAvail = quint64(double(m.memTotal) * (0.62 - 0.06 * std::sin(t * 0.11)));
    m.swapTotal = 8589934592ULL;
    m.swapFree = m.swapTotal - quint64(double(m.swapTotal) * (0.12 + 0.05 * std::sin(t * 0.07)));

    m.net.rxBps = 2400000.0 + 2100000.0 * std::sin(t * 0.9);
    m.net.txBps = 180000.0 + 150000.0 * std::sin(t * 1.7 + 1.0);
    m.disk.readBps = 900000.0 + 800000.0 * std::sin(t * 1.1 + 2.0);
    m.disk.writeBps = 1400000.0 + 1200000.0 * std::sin(t * 0.8);

    TempSample t1{QStringLiteral("coretemp · Package id 0"), 71.0 + 6.0 * std::sin(t * 0.6)};
    TempSample t2{QStringLiteral("coretemp · Core 0"), 66.0 + 5.0 * std::sin(t * 0.6 + 0.4)};
    TempSample t3{QStringLiteral("nvme · Composite"), 41.0 + 2.0 * std::sin(t * 0.2)};
    m.temps << t1 << t2 << t3;

    m.batteryPresent = true;
    m.batteryPct = 78.0;
    m.charging = true;
    m.mains = true;
    m.pressureCpu = std::clamp(m.cpuPct * 0.12, 0.0, 100.0);

    struct P {
        const char *name;
        double load;
        quint64 rss;
    };
    const P pool[] = {
        {"firefox", 14.0, 1400000000ULL},
        {"hermes", 6.4, 420000000ULL},
        {"plasmashell", 3.1, 310000000ULL},
        {"code", 2.2, 890000000ULL},
        {"Xwayland", 1.4, 180000000ULL},
        {"pipewire", 0.9, 42000000ULL},
        {"kwin_wayland", 2.6, 260000000ULL},
        {"konsole", 0.6, 96000000ULL},
    };
    int pid = 1041;
    for (const P &p : pool) {
        ProcSample s;
        s.name = QString::fromLatin1(p.name);
        s.pid = pid;
        pid += 317 + int(p.rss / 1000000ULL) * 13;
        s.rss = p.rss;
        s.cpuPct = std::clamp(p.load * (0.8 + 0.4 * std::sin(t + p.load)), 0.0, 100.0);
        m.procs << s;
    }
    return m;
}

QRectF Dashboard::toggleRect(int which) const
{
    if (which < 0 || which >= m_layout.toggles.size())
        return {};
    return m_layout.toggles.at(which);
}

void Dashboard::buildLayout()
{
    const qreal W = width();
    const qreal H = height();
    const qreal m = kMargin;
    const qreal g = kGap;

    m_layout.header = QRectF(m, m, std::max(0.0, W - 2 * m), kHeaderH);
    m_layout.footer = QRectF(m, H - m - kFooterH, std::max(0.0, W - 2 * m), kFooterH);

    const qreal bodyTop = m_layout.header.bottom() + g;
    const qreal bodyH = std::max(0.0, m_layout.footer.top() - g - bodyTop);
    const QRectF body(m, bodyTop, std::max(0.0, W - 2 * m), bodyH);

    const qreal bh = std::max(0.0, body.height() - 2 * g);
    const qreal h1 = bh * 0.40;
    const qreal h2 = bh * 0.27;
    const qreal h3 = std::max(0.0, bh - h1 - h2);

    const qreal wLeft = (body.width() - g) * 0.60;
    m_layout.cpu = QRectF(body.left(), body.top(), wLeft, h1);
    m_layout.mem = QRectF(body.left() + wLeft + g, body.top(),
                          std::max(0.0, body.width() - wLeft - g), h1);

    const qreal row2Y = body.top() + h1 + g;
    const qreal bw = std::max(0.0, body.width() - 2 * g);
    const qreal wTemp = bw * 0.42;
    const qreal wNet = bw * 0.28;
    const qreal wDisk = std::max(0.0, bw - wTemp - wNet);
    m_layout.temp = QRectF(body.left(), row2Y, wTemp, h2);
    m_layout.net = QRectF(m_layout.temp.right() + g, row2Y, wNet, h2);
    m_layout.disk = QRectF(m_layout.net.right() + g, row2Y, wDisk, h2);

    m_layout.procs = QRectF(body.left(), row2Y + h2 + g, body.width(), h3);
    m_layout.procRows = int(std::max(0.0, (h3 - kPanelHead - 26.0)) / kProcRowH);

    // interruttori: da destra, larghezza dal testo più largo
    const QFont tf = theme::mono(10.0, true, 1.0);
    const QFontMetricsF fm(tf);
    const QString labels[ToggleCount] = {
        m_paused ? QStringLiteral("IN PAUSA") : QStringLiteral("LIVE"),
        m_sortByMem ? QStringLiteral("ORDINA MEM") : QStringLiteral("ORDINA CPU"),
        m_demo ? QStringLiteral("DEMO ON") : QStringLiteral("DEMO"),
    };
    QVector<qreal> widths;
    for (const QString &l : labels)
        widths << std::max(88.0, fm.horizontalAdvance(l) + 42.0);

    qreal x = m_layout.footer.right();
    m_layout.toggles.clear();
    for (int i = ToggleCount - 1; i >= 0; --i) {
        const qreal w = widths.at(i);
        x -= w;
        m_layout.toggles.prepend(QRectF(x, m_layout.footer.center().y() - 13.0, w, 26.0));
        x -= 8.0;
    }
    m_layout.valid = W > 0 && H > 0;
}

void Dashboard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    buildLayout();
    rebuildBackground();
}

void Dashboard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    buildLayout();
    setFocus();
}

void Dashboard::mousePressEvent(QMouseEvent *event)
{
    buildLayout();
    const QPointF pos = event->position();
    for (int i = 0; i < m_layout.toggles.size(); ++i) {
        if (!m_layout.toggles.at(i).contains(pos))
            continue;
        switch (i) {
        case Pause:
            setPaused(!m_paused);
            break;
        case SortMem:
            setSortByMem(!m_sortByMem);
            break;
        case Demo:
            setDemo(!m_demo);
            break;
        default:
            break;
        }
        return;
    }
    QWidget::mousePressEvent(event);
}

void Dashboard::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        setPaused(!m_paused);
        return;
    case Qt::Key_D:
        setDemo(!m_demo);
        return;
    case Qt::Key_M:
        setSortByMem(!m_sortByMem);
        return;
    case Qt::Key_F11:
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
        return;
    case Qt::Key_Escape:
        if (isFullScreen()) {
            showNormal();
            return;
        }
        emit quitRequested();
        return;
    case Qt::Key_Q:
        emit quitRequested();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

bool Dashboard::saveCapture(const QString &path)
{
    const QPixmap pix = grab();
    return pix.save(path);
}
