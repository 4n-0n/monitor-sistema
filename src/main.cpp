#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "core/format.h"
#include "core/telemetry.h"
#include "ui/dashboard.h"

namespace {

const char *kVersion = "1.0.0";

// I flag headless non devono costruire QApplication: su una macchina senza
// display (SSH, cron, CI) morirebbe con "could not connect to display".
bool wantsHeadless(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--once") || a == QLatin1String("-o")
            || a == QLatin1String("--json") || a == QLatin1String("-j")
            || a == QLatin1String("--help") || a == QLatin1String("-h")
            || a == QLatin1String("--help-all") || a == QLatin1String("--version")
            || a == QLatin1String("-v"))
            return true;
    }
    return false;
}

void addOptions(QCommandLineParser &parser)
{
    parser.setApplicationDescription(
        QStringLiteral("Monitor di sistema: CPU, memoria, temperature, rete, disco, processi."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption({QStringLiteral("o"), QStringLiteral("once")},
                                        QStringLiteral("Stampa un campione ed esce (headless).")));
    parser.addOption(QCommandLineOption({QStringLiteral("j"), QStringLiteral("json")},
                                        QStringLiteral("Come --once, ma in JSON.")));
    parser.addOption(QCommandLineOption(QStringLiteral("demo"),
                                        QStringLiteral("Dati sintetici deterministici (nessuna lettura di /proc).")));
    parser.addOption(QCommandLineOption(QStringLiteral("capture"),
                                        QStringLiteral("Salva un PNG della finestra ed esce."),
                                        QStringLiteral("file.png")));
    parser.addOption(QCommandLineOption(QStringLiteral("size"),
                                        QStringLiteral("Dimensione finestra, es. 1280x800."),
                                        QStringLiteral("WxH")));
    parser.addOption(QCommandLineOption(QStringLiteral("wait"),
                                        QStringLiteral("Attesa prima della cattura, in ms."),
                                        QStringLiteral("ms"), QStringLiteral("900")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("interval"),
        QStringLiteral("Intervallo di campionamento in ms (default 200 = 5 al secondo, "
                       "minimo 20 = 50 al secondo)."),
        QStringLiteral("ms"), QStringLiteral("200")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("window"),
        QStringLiteral("Secondi di storico nei grafici (default 60; più basso = scorre più "
                       "veloce)."),
        QStringLiteral("sec"), QStringLiteral("60")));
}

QJsonObject machineToJson(const Machine &m)
{
    QJsonObject o;
    o[QStringLiteral("host")] = m.host;
    o[QStringLiteral("kernel")] = m.kernel;
    o[QStringLiteral("uptime_s")] = m.uptimeSec;
    o[QStringLiteral("cpu_pct")] = m.cpuPct;
    o[QStringLiteral("freq_mhz")] = m.freqMhz;
    o[QStringLiteral("load")] = QJsonArray{m.load1, m.load5, m.load15};
    o[QStringLiteral("procs")] = QJsonArray{m.procsRunning, m.procsTotal};
    o[QStringLiteral("mem_total")] = double(m.memTotal);
    o[QStringLiteral("mem_available")] = double(m.memAvail);
    o[QStringLiteral("mem_used_pct")] = m.memUsedPct();
    o[QStringLiteral("swap_total")] = double(m.swapTotal);
    o[QStringLiteral("swap_free")] = double(m.swapFree);
    o[QStringLiteral("net_rx_bps")] = m.net.rxBps;
    o[QStringLiteral("net_tx_bps")] = m.net.txBps;
    o[QStringLiteral("disk_read_bps")] = m.disk.readBps;
    o[QStringLiteral("disk_write_bps")] = m.disk.writeBps;
    o[QStringLiteral("pressure_cpu")] = m.pressureCpu;
    o[QStringLiteral("battery_pct")] = m.batteryPresent ? m.batteryPct : -1.0;
    o[QStringLiteral("battery_charging")] = m.charging;

    QJsonArray cores;
    for (const CoreSample &c : m.cores)
        cores.append(c.pct);
    o[QStringLiteral("cores")] = cores;

    QJsonArray temps;
    for (const TempSample &t : m.temps) {
        QJsonObject o2;
        o2[QStringLiteral("label")] = t.label;
        o2[QStringLiteral("celsius")] = t.celsius;
        temps.append(o2);
    }
    o[QStringLiteral("temps")] = temps;

    QJsonArray procs;
    for (const ProcSample &p : m.procs) {
        QJsonObject o2;
        o2[QStringLiteral("pid")] = p.pid;
        o2[QStringLiteral("name")] = p.name;
        o2[QStringLiteral("cpu_pct")] = p.cpuPct;
        o2[QStringLiteral("rss")] = double(p.rss);
        procs.append(o2);
    }
    o[QStringLiteral("top")] = procs;
    return o;
}

int runHeadless(bool json)
{
    Telemetry tel;
    tel.sample();
    QThread::msleep(1000);          // due campioni per avere i delta
    const Machine m = tel.sample();

    QTextStream out(stdout);
    if (json) {
        const QJsonObject o = machineToJson(m);
        out << QJsonDocument(o).toJson(QJsonDocument::Indented);
        return 0;
    }

    out << "host      " << m.host << "  (" << m.kernel << ")\n";
    out << "uptime    " << fmt::duration(m.uptimeSec) << "\n";
    out << "cpu       " << (m.cpuPct < 0 ? QStringLiteral("n/d") : fmt::pct(m.cpuPct))
        << "  (" << m.cores.size() << " core, " << fmt::mhz(m.freqMhz) << ")\n";
    QStringList cores;
    for (const CoreSample &c : m.cores)
        cores << fmt::num(std::max(0.0, c.pct), 0);
    out << "per core  " << cores.join(QLatin1Char(' ')) << "\n";
    out << "load      " << fmt::num(m.load1, 2) << ' ' << fmt::num(m.load5, 2) << ' '
        << fmt::num(m.load15, 2) << "   " << m.procsRunning << '/' << m.procsTotal << " proc\n";
    out << "mem       " << fmt::bytes(m.memTotal - m.memAvail) << " / " << fmt::bytes(m.memTotal)
        << "  (" << fmt::pct(m.memUsedPct()) << ")\n";
    out << "swap      " << fmt::bytes(m.swapTotal - m.swapFree) << " / " << fmt::bytes(m.swapTotal)
        << "\n";
    out << "net       rx " << fmt::rate(m.net.rxBps) << "   tx " << fmt::rate(m.net.txBps) << "\n";
    out << "disk      r " << fmt::rate(m.disk.readBps) << "   w " << fmt::rate(m.disk.writeBps)
        << "\n";
    QStringList temps;
    for (const TempSample &t : m.temps)
        temps << QStringLiteral("%1 %2 °C").arg(t.label, fmt::num(t.celsius, 0));
    out << "temp      " << (temps.isEmpty() ? QStringLiteral("nessun sensore") : temps.join(QStringLiteral(" · ")))
        << "\n";
    out << "pressure  " << fmt::pct(m.pressureCpu) << "\n";
    if (m.batteryPresent)
        out << "battery   " << fmt::pct(m.batteryPct) << (m.charging ? " (in carica)" : " (a batteria)")
            << "\n";
    QStringList procs;
    for (const ProcSample &p : m.procs)
        procs << QStringLiteral("%1 %2 (%3)").arg(p.name, fmt::pct(p.cpuPct), fmt::bytes(p.rss));
    out << "top       " << procs.join(QStringLiteral("  ·  ")) << "\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("monitor-sistema"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kVersion));
    QCoreApplication::setOrganizationName(QStringLiteral("cybernet"));

    const bool headless = wantsHeadless(argc, argv);
    QScopedPointer<QCoreApplication> app(headless ? new QCoreApplication(argc, argv)
                                                  : new QApplication(argc, argv));

    // app_id = nome del file .desktop: senza, il dock mostra l'icona generica
    if (!headless) {
        if (auto *gui = qobject_cast<QGuiApplication *>(app.data()))
            gui->setDesktopFileName(QStringLiteral("monitor-sistema"));
    }

    QCommandLineParser parser;
    addOptions(parser);
    parser.process(*app);

    if (parser.isSet(QStringLiteral("once")) || parser.isSet(QStringLiteral("json")))
        return runHeadless(parser.isSet(QStringLiteral("json")));

    auto *dash = new Dashboard;
    bool intervalOk = false;
    const int intervalMs = parser.value(QStringLiteral("interval")).toInt(&intervalOk);
    if (intervalOk)
        dash->setIntervalMs(intervalMs);
    bool windowOk = false;
    const int windowSec = parser.value(QStringLiteral("window")).toInt(&windowOk);
    if (windowOk)
        dash->setWindowSec(windowSec);
    if (parser.isSet(QStringLiteral("demo")))
        dash->setDemo(true);

    QSize size(1280, 820);
    if (parser.isSet(QStringLiteral("size"))) {
        const QString spec = parser.value(QStringLiteral("size")).toLower();
        const QStringList wh = spec.split(QLatin1Char('x'));
        bool okW = false, okH = false;
        if (wh.size() == 2) {
            const int w = wh.at(0).toInt(&okW);
            const int h = wh.at(1).toInt(&okH);
            if (okW && okH && w >= 320 && h >= 240)
                size = QSize(w, h);
        }
    }
    dash->resize(size);
    dash->show();

    QObject::connect(dash, &Dashboard::quitRequested, app.data(), &QCoreApplication::quit);

    if (parser.isSet(QStringLiteral("capture"))) {
        const QString path = parser.value(QStringLiteral("capture"));
        int waitMs = parser.value(QStringLiteral("wait")).toInt();
        if (waitMs < 0)
            waitMs = 0;
        QTimer::singleShot(waitMs, app.data(), [dash, path, app = app.data()]() {
            QTextStream out(stdout);
            const bool ok = dash->saveCapture(path);
            out << (ok ? "cattura salvata: " : "cattura FALLITA: ") << path << '\n';
            out.flush();
            app->exit(ok ? 0 : 1);
        });
    }

    const int rc = app->exec();
    delete dash;
    return rc;
}
