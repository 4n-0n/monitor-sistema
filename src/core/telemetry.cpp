#include "telemetry.h"

#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QDateTime>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace {

QByteArray slurp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    // /proc e /sys riportano st_size 0: mai usare QTextStream/atEnd, si ferma subito.
    return f.readAll();
}

QStringList splitLines(const QByteArray &raw)
{
    return QString::fromLatin1(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString readOneLine(const QString &path)
{
    const QByteArray raw = slurp(path);
    return QString::fromLatin1(raw).trimmed();
}

bool isVirtualInterface(const QString &name)
{
    static const char *prefixes[] = {"lo", "docker", "br-", "veth", "virbr",
                                     "zt", "tun", "tap", "wg", "tailscale",
                                     "vmnet", "vnet", "cni", "flannel", "dummy"};
    for (const char *p : prefixes) {
        if (name.startsWith(QLatin1String(p)))
            return true;
    }
    return false;
}

bool isWholeDisk(const QString &name)
{
    // solo dischi interi: le partizioni (sda1, nvme0n1p2) contano due volte
    if (name.startsWith(QLatin1String("nvme")))
        return !name.contains(QLatin1Char('p'));
    if (name.startsWith(QLatin1String("mmcblk")))
        return !name.contains(QLatin1Char('p'));
    if (name.startsWith(QLatin1String("sd")) || name.startsWith(QLatin1String("vd")))
        return name.size() == 3;   // sda, vdb — non sda1
    return false;
}

} // namespace

double Machine::maxTemp() const
{
    double t = 0.0;
    for (const TempSample &s : temps)
        t = std::max(t, s.celsius);
    return t;
}

double Machine::memUsedPct() const
{
    if (memTotal == 0)
        return 0.0;
    return double(memTotal - memAvail) * 100.0 / double(memTotal);
}

double Machine::swapUsedPct() const
{
    if (swapTotal == 0)
        return 0.0;
    return double(swapTotal - swapFree) * 100.0 / double(swapTotal);
}

Telemetry::Telemetry(QString procRoot, QString sysRoot)
    : m_proc(std::move(procRoot)), m_sys(std::move(sysRoot))
{
}

Machine Telemetry::sample()
{
    Machine m;
    m.host = QSysInfo::machineHostName();
    m.kernel = QSysInfo::kernelVersion();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    double dt = 0.0;
    if (m_havePrev && now > m_lastMsecs)
        dt = double(now - m_lastMsecs) / 1000.0;

    readCpu(m, dt);
    readMem(m);
    readLoad(m);
    readUptime(m);
    readPressure(m);
    readFreq(m);
    readNet(m, dt);
    readDisk(m, dt);
    readTemps(m);
    readPower(m);

    // processi: scansione a intervallo proprio, riuso della lista nei campioni rapidi
    const bool scanProcs = !m_procScanned || (now - m_lastProcMsecs) >= m_procIntervalMs;
    if (scanProcs) {
        const double procDt = m_procScanned ? double(now - m_lastProcMsecs) / 1000.0 : 0.0;
        readProcs(m, procDt);
        m_lastProcs = m.procs;
        m_lastProcMsecs = now;
        m_procScanned = true;
    } else {
        m.procs = m_lastProcs;
    }

    m.valid = !m.cores.isEmpty();
    m_havePrev = true;
    m_lastMsecs = now;
    return m;
}

void Telemetry::readCpu(Machine &m, double dt)
{
    const QStringList lines = splitLines(slurp(m_proc + QStringLiteral("/stat")));
    QVector<CpuTicks> ticks;
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("cpu")))
            continue;
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 5)
            continue;
        quint64 v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 1; i < f.size() && i <= 8; ++i)
            v[i - 1] = f.at(i).toULongLong();
        CpuTicks t;
        t.busy = v[0] + v[1] + v[2] + v[5] + v[6] + v[7];   // user nice system irq softirq steal
        t.idle = v[3] + v[4];                                // idle iowait
        ticks << t;
    }
    if (ticks.isEmpty())
        return;

    m.cpuPct = -1.0;
    for (int i = 0; i < ticks.size(); ++i) {
        const CpuTicks &cur = ticks.at(i);
        double used = -1.0;
        if (m_havePrev && i < m_prevCpu.size() && dt > 0.0) {
            const CpuTicks &prev = m_prevCpu.at(i);
            const quint64 dbusy = cur.busy >= prev.busy ? cur.busy - prev.busy : 0;
            const quint64 didle = cur.idle >= prev.idle ? cur.idle - prev.idle : 0;
            const quint64 dtotal = dbusy + didle;
            if (dtotal > 0)
                used = double(dbusy) * 100.0 / double(dtotal);
        }
        if (i == 0) {
            m.cpuPct = used;
            continue;
        }
        CoreSample c;
        c.pct = used;
        m.cores << c;
    }

    // frequenza per core: media dei "cpu MHz" di /proc/cpuinfo (sempre leggibile)
    double mhzSum = 0.0;
    int mhzCount = 0;
    for (const QString &line : splitLines(slurp(m_proc + QStringLiteral("/cpuinfo")))) {
        if (!line.startsWith(QLatin1String("cpu MHz")))
            continue;
        const QString v = line.section(QLatin1Char(':'), 1, 1).trimmed();
        bool ok = false;
        const double mhz = v.toDouble(&ok);
        if (ok) {
            mhzSum += mhz;
            ++mhzCount;
        }
    }
    m.freqMhz = mhzCount > 0 ? mhzSum / mhzCount : 0.0;
    for (CoreSample &c : m.cores)
        c.mhz = m.freqMhz;

    m_prevCpu = ticks;
}

void Telemetry::readMem(Machine &m)
{
    quint64 freeKb = 0, buffers = 0, cached = 0, sreclaimable = 0;
    for (const QString &line : splitLines(slurp(m_proc + QStringLiteral("/meminfo")))) {
        const QString key = line.section(QLatin1Char(':'), 0, 0);
        const QString val = line.section(QLatin1Char(':'), 1, 1).trimmed()
                                .section(QLatin1Char(' '), 0, 0);
        const quint64 kb = val.toULongLong();
        if (key == QLatin1String("MemTotal"))
            m.memTotal = kb * 1024;
        else if (key == QLatin1String("MemAvailable"))
            m.memAvail = kb * 1024;
        else if (key == QLatin1String("MemFree"))
            freeKb = kb;
        else if (key == QLatin1String("Buffers"))
            buffers = kb;
        else if (key == QLatin1String("Cached"))
            cached = kb;
        else if (key == QLatin1String("SReclaimable"))
            sreclaimable = kb;
        else if (key == QLatin1String("SwapTotal"))
            m.swapTotal = kb * 1024;
        else if (key == QLatin1String("SwapFree"))
            m.swapFree = kb * 1024;
    }
    if (m.memAvail == 0 && m.memTotal > 0)
        m.memAvail = (freeKb + buffers + cached + sreclaimable) * 1024;
}

void Telemetry::readLoad(Machine &m)
{
    const QString line = readOneLine(m_proc + QStringLiteral("/loadavg"));
    if (line.isEmpty())
        return;
    const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (f.size() >= 3) {
        m.load1 = f.at(0).toDouble();
        m.load5 = f.at(1).toDouble();
        m.load15 = f.at(2).toDouble();
    }
    if (f.size() >= 4) {
        const QString procs = f.at(3);
        m.procsRunning = procs.section(QLatin1Char('/'), 0, 0).toInt();
        m.procsTotal = procs.section(QLatin1Char('/'), 1, 1).toInt();
    }
}

void Telemetry::readUptime(Machine &m)
{
    const QString line = readOneLine(m_proc + QStringLiteral("/uptime"));
    m.uptimeSec = line.section(QLatin1Char(' '), 0, 0).toDouble();
}

void Telemetry::readPressure(Machine &m)
{
    const QString line = readOneLine(m_proc + QStringLiteral("/pressure/cpu"));
    const QString some = line.section(QStringLiteral("some"), 1, 1);
    const QString avg10 = some.section(QStringLiteral("avg10="), 1, 1)
                              .section(QLatin1Char(' '), 0, 0);
    bool ok = false;
    const double v = avg10.toDouble(&ok);
    m.pressureCpu = ok ? v : 0.0;
}

void Telemetry::readFreq(Machine &m)
{
    if (m.freqMhz > 0.0)
        return;
    // fallback sysfs: frequenza del primo core in kHz
    const QString path = m_sys + QStringLiteral("/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    bool ok = false;
    const double khz = readOneLine(path).toDouble(&ok);
    if (ok && khz > 0.0)
        m.freqMhz = khz / 1000.0;
}

void Telemetry::readNet(Machine &m, double dt)
{
    quint64 rx = 0, tx = 0;
    for (const QString &line : splitLines(slurp(m_proc + QStringLiteral("/net/dev")))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString iface = line.left(colon).trimmed();
        if (iface.isEmpty() || isVirtualInterface(iface))
            continue;
        const QStringList f = line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 9)
            continue;
        rx += f.at(0).toULongLong();
        tx += f.at(8).toULongLong();
    }
    if (m_havePrev && dt > 0.0) {
        m.net.rxBps = rx >= m_prevRx ? double(rx - m_prevRx) / dt : 0.0;
        m.net.txBps = tx >= m_prevTx ? double(tx - m_prevTx) / dt : 0.0;
    }
    m_prevRx = rx;
    m_prevTx = tx;
}

void Telemetry::readDisk(Machine &m, double dt)
{
    quint64 rd = 0, wr = 0;
    for (const QString &line : splitLines(slurp(m_proc + QStringLiteral("/diskstats")))) {
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 10)
            continue;
        const QString name = f.at(2);
        if (!isWholeDisk(name))
            continue;
        rd += f.at(5).toULongLong() * 512ULL;   // settori letti
        wr += f.at(9).toULongLong() * 512ULL;   // settori scritti
    }
    if (m_havePrev && dt > 0.0) {
        m.disk.readBps = rd >= m_prevRd ? double(rd - m_prevRd) / dt : 0.0;
        m.disk.writeBps = wr >= m_prevWr ? double(wr - m_prevWr) / dt : 0.0;
    }
    m_prevRd = rd;
    m_prevWr = wr;
}

void Telemetry::readTemps(Machine &m)
{
    const QString base = m_sys + QStringLiteral("/class/hwmon");
    const QStringList dirs = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dir : dirs) {
        if (!dir.startsWith(QLatin1String("hwmon")))
            continue;
        const QString hw = base + QLatin1Char('/') + dir;
        const QString chip = readOneLine(hw + QStringLiteral("/name"));
        const QStringList files = QDir(hw).entryList(QStringList{QStringLiteral("temp*_input")},
                                                     QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QString index = file.section(QStringLiteral("temp"), 1, 1)
                                      .section(QLatin1Char('_'), 0, 0);
            const QString raw = readOneLine(hw + QLatin1Char('/') + file);
            bool ok = false;
            const double milli = raw.toDouble(&ok);
            if (!ok)
                continue;
            const double c = milli / 1000.0;
            if (c <= 0.0 || c > 150.0)
                continue;
            QString label = readOneLine(hw + QStringLiteral("/temp") + index
                                        + QStringLiteral("_label"));
            if (label.isEmpty())
                label = chip.isEmpty() ? dir : chip;
            else if (!chip.isEmpty())
                label = chip + QStringLiteral(" · ") + label;
            TempSample t;
            t.label = label;
            t.celsius = c;
            m.temps << t;
        }
    }
    std::sort(m.temps.begin(), m.temps.end(),
              [](const TempSample &a, const TempSample &b) { return a.celsius > b.celsius; });
    while (m.temps.size() > 6)
        m.temps.removeLast();
}

void Telemetry::readPower(Machine &m)
{
    const QString base = m_sys + QStringLiteral("/class/power_supply");
    const QStringList dirs = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dir : dirs) {
        const QString dev = base + QLatin1Char('/') + dir;
        const QString type = readOneLine(dev + QStringLiteral("/type"));
        if (type == QLatin1String("Battery")) {
            bool ok = false;
            const double cap = readOneLine(dev + QStringLiteral("/capacity")).toDouble(&ok);
            if (ok) {
                m.batteryPresent = true;
                m.batteryPct = cap;
                const QString status = readOneLine(dev + QStringLiteral("/status"));
                m.charging = status == QLatin1String("Charging")
                             || status == QLatin1String("Full");
            }
        } else if (type == QLatin1String("Mains")) {
            m.mains = readOneLine(dev + QStringLiteral("/online")) == QLatin1String("1");
        }
    }
}

void Telemetry::readProcs(Machine &m, double dt)
{
    const double clkTck = double(sysconf(_SC_CLK_TCK));
    const double pageSize = double(sysconf(_SC_PAGESIZE));
    if (clkTck <= 0.0 || pageSize <= 0.0)
        return;

    const QStringList entries = QDir(m_proc).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QHash<int, quint64> ticksNow;
    QVector<ProcSample> all;
    all.reserve(entries.size());
    for (const QString &entry : entries) {
        bool isNum = false;
        const int pid = entry.toInt(&isNum);
        if (!isNum)
            continue;
        const QByteArray raw = slurp(m_proc + QLatin1Char('/') + entry + QStringLiteral("/stat"));
        if (raw.isEmpty())
            continue;
        const QString line = QString::fromLatin1(raw);
        const int close = line.lastIndexOf(QLatin1Char(')'));
        const int open = line.indexOf(QLatin1Char('('));
        if (close <= open + 1)
            continue;
        ProcSample p;
        p.pid = pid;
        p.name = line.mid(open + 1, close - open - 1);
        const QStringList f = line.mid(close + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 22)
            continue;
        const quint64 utime = f.at(11).toULongLong();
        const quint64 stime = f.at(12).toULongLong();
        const quint64 ticks = utime + stime;
        ticksNow.insert(pid, ticks);
        p.rss = quint64(f.at(21).toLongLong()) * quint64(pageSize);
        if (dt > 0.0) {
            const auto it = m_prevProcTicks.constFind(pid);
            if (it != m_prevProcTicks.constEnd() && ticks >= it.value())
                p.cpuPct = double(ticks - it.value()) / clkTck / dt * 100.0;
        }
        all << p;
    }
    m_prevProcTicks = ticksNow;

    const int nCores = std::max(1, int(m.cores.size()));
    std::sort(all.begin(), all.end(), [](const ProcSample &a, const ProcSample &b) {
        if (std::abs(a.cpuPct - b.cpuPct) > 0.01)
            return a.cpuPct > b.cpuPct;
        return a.rss > b.rss;
    });
    for (const ProcSample &p : all) {
        if (m.procs.size() >= 8)
            break;
        // CPU normalizzata sul numero di core, così "100%" vuol dire tutta la macchina
        ProcSample q = p;
        q.cpuPct = p.cpuPct / double(nCores);
        m.procs << q;
    }
}
