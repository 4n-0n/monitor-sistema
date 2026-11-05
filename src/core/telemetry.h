#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QtGlobal>

// Telemetria di sistema letta da /proc e /sys.
// Nessuna dipendenza da Qt Widgets: si testa senza display e senza la macchina reale.

struct CoreSample {
    double pct = -1.0;   // -1 = primo campione, delta non disponibile
    double mhz = 0.0;
};

struct TempSample {
    QString label;
    double celsius = 0.0;
};

struct NetSample {
    double rxBps = 0.0;
    double txBps = 0.0;
};

struct DiskSample {
    double readBps = 0.0;
    double writeBps = 0.0;
};

struct ProcSample {
    int pid = 0;
    QString name;
    double cpuPct = 0.0;
    quint64 rss = 0;     // byte
};

struct Machine {
    bool valid = false;
    QString host;
    QString kernel;

    double cpuPct = -1.0;
    QVector<CoreSample> cores;
    double freqMhz = 0.0;

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    int procsRunning = 0, procsTotal = 0;

    quint64 memTotal = 0, memAvail = 0;
    quint64 swapTotal = 0, swapFree = 0;

    NetSample net;
    DiskSample disk;

    QVector<TempSample> temps;
    QVector<ProcSample> procs;

    double uptimeSec = 0.0;
    double pressureCpu = 0.0;   // avg10 di /proc/pressure/cpu, 0 se assente

    bool batteryPresent = false;
    double batteryPct = 0.0;
    bool charging = false;
    bool mains = false;

    double maxTemp() const;
    double memUsedPct() const;
    double swapUsedPct() const;
};

class Telemetry
{
public:
    // radici finte per i test: fixture di /proc e /sys in una QTemporaryDir
    explicit Telemetry(QString procRoot = QStringLiteral("/proc"),
                       QString sysRoot = QStringLiteral("/sys"));

    // La scansione di /proc/<pid>/stat costa (migliaia di file): la si tiene a 1 s
    // anche quando il campionamento è più rapido, e si riusa l'ultima lista.
    void setProcIntervalMs(int ms) { m_procIntervalMs = ms > 0 ? ms : 1000; }
    int procIntervalMs() const { return m_procIntervalMs; }

    Machine sample();

private:
    struct CpuTicks {
        quint64 busy = 0;
        quint64 idle = 0;
    };

    void readCpu(Machine &m, double dt);
    void readMem(Machine &m);
    void readLoad(Machine &m);
    void readUptime(Machine &m);
    void readPressure(Machine &m);
    void readFreq(Machine &m);
    void readNet(Machine &m, double dt);
    void readDisk(Machine &m, double dt);
    void readTemps(Machine &m);
    void readPower(Machine &m);
    void readProcs(Machine &m, double dt);

    QString m_proc;
    QString m_sys;

    bool m_havePrev = false;
    qint64 m_lastMsecs = 0;

    QVector<CpuTicks> m_prevCpu;
    quint64 m_prevRx = 0, m_prevTx = 0;
    quint64 m_prevRd = 0, m_prevWr = 0;
    QHash<int, quint64> m_prevProcTicks;

    int m_procIntervalMs = 1000;
    qint64 m_lastProcMsecs = 0;
    bool m_procScanned = false;
    QVector<ProcSample> m_lastProcs;
};
