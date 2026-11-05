#include <QtTest>

#include "core/format.h"
#include "core/telemetry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <unistd.h>

class TestCore : public QObject
{
    Q_OBJECT
private slots:
    void parseFixture();
    void formatting();
    void batteryAndProcessNames();
    void rapidSamplesReuseProcs();

private:
    static bool put(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return f.write(data) == data.size();
    }

    void buildFixture();
    QTemporaryDir m_tmp;
    QString m_proc;
    QString m_sys;
};

void TestCore::buildFixture()
{
    QVERIFY(m_tmp.isValid());
    m_proc = m_tmp.path() + QStringLiteral("/proc");
    m_sys = m_tmp.path() + QStringLiteral("/sys");

    QVERIFY(put(m_proc + "/stat",
                "cpu  100 0 100 800 0 0 0 0 0 0\n"
                "cpu0 50 0 50 400 0 0 0 0 0 0\n"
                "cpu1 50 0 50 400 0 0 0 0 0 0\n"
                "intr 1 2 3\nctxt 999\n"));
    QVERIFY(put(m_proc + "/meminfo",
                "MemTotal:        8000000 kB\n"
                "MemFree:         1000000 kB\n"
                "MemAvailable:    6000000 kB\n"
                "Buffers:          200000 kB\n"
                "Cached:          1500000 kB\n"
                "SwapTotal:       2000000 kB\n"
                "SwapFree:        1600000 kB\n"));
    QVERIFY(put(m_proc + "/loadavg", "0.50 0.40 0.30 2/345 12345\n"));
    QVERIFY(put(m_proc + "/uptime", "12345.67 50000.00\n"));
    QVERIFY(put(m_proc + "/pressure/cpu",
                "some avg10=1.25 avg60=0.50 avg300=0.10 total=1000\n"
                "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"));
    QVERIFY(put(m_proc + "/cpuinfo",
                "processor\t: 0\ncpu MHz\t\t: 2400.000\n"
                "processor\t: 1\ncpu MHz\t\t: 2600.000\n"));
    QVERIFY(put(m_proc + "/net/dev",
                "Inter-|   Receive                                                |  Transmit\n"
                " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                "    lo: 9999999 0 0 0 0 0 0 0 9999999 0 0 0 0 0 0 0\n"
                "docker0: 5000 0 0 0 0 0 0 0 5000 0 0 0 0 0 0 0\n"
                "  eth0: 1000 0 0 0 0 0 0 0 500 0 0 0 0 0 0 0\n"));
    QVERIFY(put(m_proc + "/diskstats",
                " 259 0 nvme0n1 100 0 2000 0 50 0 1000 0 0 0 0 0 0\n"
                " 259 1 nvme0n1p1 999 0 999999 0 999 0 999999 0 0 0 0 0 0\n"
                "   8 0 sda 10 0 100 0 10 0 100 0 0 0 0 0 0\n"));

    QVERIFY(put(m_sys + "/class/hwmon/hwmon0/name", "coretemp\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon0/temp1_input", "45000\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon0/temp1_label", "Package id 0\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon0/temp2_input", "39000\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon0/temp2_label", "Core 0\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon1/name", "nvme\n"));
    QVERIFY(put(m_sys + "/class/hwmon/hwmon1/temp1_input", "40000\n"));

    QVERIFY(put(m_sys + "/class/power_supply/BAT0/type", "Battery\n"));
    QVERIFY(put(m_sys + "/class/power_supply/BAT0/capacity", "82\n"));
    QVERIFY(put(m_sys + "/class/power_supply/BAT0/status", "Discharging\n"));
    QVERIFY(put(m_sys + "/class/power_supply/AC/type", "Mains\n"));
    QVERIFY(put(m_sys + "/class/power_supply/AC/online", "0\n"));

    QVERIFY(put(m_proc + "/1234/stat",
                "1234 (firefox) S 1 1234 1234 0 -1 4194304 1000 0 0 0 100 50 0 0 20 0 1 0 1000 100000 500 0\n"));
    QVERIFY(put(m_proc + "/99/stat",
                "99 (my app (x)) S 1 99 99 0 -1 4194304 100 0 0 0 10 5 0 0 20 0 1 0 1000 100000 50 0\n"));
}

void TestCore::parseFixture()
{
    buildFixture();
    Telemetry tel(m_proc, m_sys);
    tel.setProcIntervalMs(500);   // la scansione deve avvenire al secondo campione

    const Machine first = tel.sample();
    QVERIFY(first.valid);
    QCOMPARE(first.cores.size(), 2);
    // primo campione: nessun delta, i tassi restano a zero
    QCOMPARE(first.cpuPct, -1.0);
    QCOMPARE(first.net.rxBps, 0.0);
    QCOMPARE(first.disk.readBps, 0.0);

    // secondo giro: raddoppio dei tick -> 20% di CPU
    QVERIFY(put(m_proc + "/stat",
                "cpu  200 0 200 1600 0 0 0 0 0 0\n"
                "cpu0 100 0 100 800 0 0 0 0 0 0\n"
                "cpu1 100 0 100 800 0 0 0 0 0 0\n"));
    QVERIFY(put(m_proc + "/net/dev",
                "  lo: 9999999 0 0 0 0 0 0 0 9999999 0 0 0 0 0 0 0\n"
                "docker0: 5000 0 0 0 0 0 0 0 5000 0 0 0 0 0 0 0\n"
                "  eth0: 3000 0 0 0 0 0 0 0 900 0 0 0 0 0 0 0\n"));
    QVERIFY(put(m_proc + "/diskstats",
                " 259 0 nvme0n1 200 0 3000 0 50 0 3000 0 0 0 0 0 0\n"
                " 259 1 nvme0n1p1 999 0 999999 0 999 0 999999 0 0 0 0 0 0\n"));
    QVERIFY(put(m_proc + "/1234/stat",
                "1234 (firefox) S 1 1234 1234 0 -1 4194304 1000 0 0 0 200 100 0 0 20 0 1 0 1000 100000 500 0\n"));

    QThread::msleep(1000);
    const Machine m = tel.sample();

    QCOMPARE(m.cores.size(), 2);
    QVERIFY2(std::abs(m.cpuPct - 20.0) < 0.01, qPrintable(fmt::num(m.cpuPct, 3)));
    QVERIFY2(std::abs(m.cores.at(0).pct - 20.0) < 0.01, qPrintable(fmt::num(m.cores.at(0).pct, 3)));

    QCOMPARE(m.memTotal, quint64(8000000) * 1024);
    QVERIFY2(std::abs(m.memUsedPct() - 25.0) < 0.001, qPrintable(fmt::num(m.memUsedPct(), 3)));
    QCOMPARE(m.swapTotal, quint64(2000000) * 1024);
    QVERIFY2(std::abs(m.swapUsedPct() - 20.0) < 0.001, "");

    QCOMPARE(m.load1, 0.5);
    QCOMPARE(m.procsRunning, 2);
    QCOMPARE(m.procsTotal, 345);
    QVERIFY2(std::abs(m.uptimeSec - 12345.67) < 0.01, "");
    QVERIFY2(std::abs(m.pressureCpu - 1.25) < 0.001, "");

    // rete: eth0 e basta, tassi positivi, rx > tx
    QVERIFY2(m.net.rxBps > m.net.txBps, qPrintable(fmt::num(m.net.rxBps, 0)));
    QVERIFY(m.net.txBps > 0.0);

    // disco: solo nvme0n1 (la partizione è esclusa), 1000 settori letti = 512000 B
    QVERIFY2(m.disk.readBps > 300000.0 && m.disk.readBps < 900000.0,
             qPrintable(fmt::num(m.disk.readBps, 0)));

    QCOMPARE(m.temps.size(), 3);
    QVERIFY2(std::abs(m.temps.at(0).celsius - 45.0) < 0.001, "");
    QVERIFY(m.temps.at(0).label.contains(QStringLiteral("Package id 0")));
    QVERIFY2(std::abs(m.maxTemp() - 45.0) < 0.001, "");

    QVERIFY(m.batteryPresent);
    QCOMPARE(m.batteryPct, 82.0);
    QVERIFY(!m.charging);
    QVERIFY(!m.mains);

    // processi: firefox in testa, CPU normalizzata sul numero di core
    QVERIFY(!m.procs.isEmpty());
    QCOMPARE(m.procs.at(0).name, QStringLiteral("firefox"));
    QCOMPARE(m.procs.at(0).rss, quint64(500) * quint64(sysconf(_SC_PAGESIZE)));
    QVERIFY2(m.procs.at(0).cpuPct > 55.0 && m.procs.at(0).cpuPct < 95.0,
             qPrintable(fmt::num(m.procs.at(0).cpuPct, 1)));
}

void TestCore::batteryAndProcessNames()
{
    buildFixture();
    Telemetry tel(m_proc, m_sys);
    tel.setProcIntervalMs(500);
    tel.sample();

    QVERIFY(put(m_sys + "/class/power_supply/AC/online", "1\n"));
    QVERIFY(put(m_sys + "/class/power_supply/BAT0/status", "Charging\n"));
    QThread::msleep(50);
    const Machine m = tel.sample();

    QVERIFY(m.charging);
    QVERIFY(m.mains);

    // comm con spazi e parentesi: si taglia all'ultima ')'
    bool found = false;
    for (const ProcSample &p : m.procs) {
        if (p.pid == 99)
            found = p.name == QStringLiteral("my app (x)");
    }
    QVERIFY(found);
}

void TestCore::rapidSamplesReuseProcs()
{
    buildFixture();
    Telemetry tel(m_proc, m_sys);
    tel.setProcIntervalMs(1000);

    const Machine a = tel.sample();
    QVERIFY(!a.procs.isEmpty());
    QCOMPARE(a.procs.at(0).name, QStringLiteral("firefox"));
    QCOMPARE(a.procs.at(0).cpuPct, 0.0);

    // tick saliti di 550, ma il campione successivo arriva dopo 60 ms:
    // la lista processi non si riscansiona e resta quella di prima
    QVERIFY(put(m_proc + "/1234/stat",
                "1234 (firefox) S 1 1234 1234 0 -1 4194304 1000 0 0 0 500 200 0 0 20 0 1 0 1000 100000 500 0\n"));
    QThread::msleep(60);
    const Machine b = tel.sample();
    QCOMPARE(b.procs.size(), a.procs.size());
    QCOMPARE(b.procs.at(0).cpuPct, 0.0);

    // passato l'intervallo, il campione riscansiona e la CPU torna a muoversi
    QThread::msleep(1100);
    const Machine c = tel.sample();
    QCOMPARE(c.procs.at(0).name, QStringLiteral("firefox"));
    QVERIFY2(c.procs.at(0).cpuPct > 0.0, qPrintable(fmt::num(c.procs.at(0).cpuPct, 1)));
}

void TestCore::formatting()
{
    QCOMPARE(fmt::num(12.34, 1), QStringLiteral("12,3"));
    QCOMPARE(fmt::bytes(1536), QStringLiteral("1,5 KB"));
    QCOMPARE(fmt::rate(2048), QStringLiteral("2,0 KB/s"));
    QCOMPARE(fmt::duration(3 * 86400 + 4 * 3600 + 12 * 60), QStringLiteral("3g 04h 12m"));
    QCOMPARE(fmt::mhz(3820), QStringLiteral("3,82 GHz"));
    QCOMPARE(fmt::mhz(840), QStringLiteral("840 MHz"));
}

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
