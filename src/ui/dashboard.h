#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>
#include <QVector>

#include "core/telemetry.h"

class QTimer;
class QPainter;

// Cruscotto dipinto interamente a mano con QPainter: nessun widget di sistema,
// nessuna immagine. Un solo buildLayout() alimenta disegno e hit-test.
class Dashboard : public QWidget
{
    Q_OBJECT
public:
    enum Toggle { Pause = 0, SortMem = 1, Demo = 2, ToggleCount = 3 };
    static constexpr int kDefaultIntervalMs = 200;   // 5 campioni al secondo
    static constexpr int kDefaultWindowSec = 60;     // quanto storico sta nel grafico

    struct Series {
        QVector<double> v;
        void push(double x, int keep);
        double last() const { return v.isEmpty() ? 0.0 : v.last(); }
    };

    struct Layout {
        bool valid = false;
        QRectF header;
        QRectF cpu, mem, temp, net, disk, procs;
        QRectF footer;
        QVector<QRectF> toggles;
        int procRows = 0;
    };

    explicit Dashboard(QWidget *parent = nullptr);
    ~Dashboard() override;

    void setDemo(bool on);
    bool demo() const { return m_demo; }
    void setPaused(bool on);
    bool paused() const { return m_paused; }
    void setSortByMem(bool on);
    bool sortByMem() const { return m_sortByMem; }

    // intervallo di campionamento in ms; la cronologia resta della stessa durata
    void setIntervalMs(int ms);
    int intervalMs() const { return m_intervalMs; }
    int capacity() const { return m_capacity; }

    // durata dello storico nel grafico: più corta = grafici che scorrono più veloci
    void setWindowSec(int seconds);
    int windowSec() const { return m_windowSec; }

    const Machine &machine() const { return m_machine; }
    const Layout &layout() const { return m_layout; }
    QRectF toggleRect(int which) const;

    // un campione in ingresso (usato dal timer, dai test e dalla demo)
    void ingest(const Machine &m);
    void fillDemoHistory(int frames);

    bool saveCapture(const QString &path);

signals:
    void quitRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onTick();

private:
    void buildLayout();
    void startTimerForMode();
    void applyCapacity();
    void rebuildBackground();
    void maybeRepaint();
    Machine demoFrame(int frame) const;

    Telemetry m_telemetry;
    Machine m_machine;
    QTimer *m_timer = nullptr;

    bool m_demo = false;
    bool m_paused = false;
    bool m_sortByMem = false;
    int m_demoFrame = 0;
    int m_intervalMs = kDefaultIntervalMs;
    int m_windowSec = kDefaultWindowSec;
    int m_capacity = (kDefaultWindowSec * 1000) / kDefaultIntervalMs;
    int m_paintIntervalMs = 100;      // il campionamento può essere più rapido del disegno
    QElapsedTimer m_paintClock;
    QPixmap m_bg;                     // gradiente + griglia, ricostruiti al resize
    qint64 m_samples = 0;             // solo per la diagnostica MON_PAINT_BENCH

    Series m_cpuHist, m_memHist, m_tempHist, m_netHist, m_diskHist;
    Layout m_layout;

    // helper di disegno (dashboard_paint.cpp)
    void paintHeader(QPainter &p);
    void paintCpu(QPainter &p, const QRectF &r);
    void paintMem(QPainter &p, const QRectF &r);
    void paintTemp(QPainter &p, const QRectF &r);
    void paintNet(QPainter &p, const QRectF &r);
    void paintDisk(QPainter &p, const QRectF &r);
    void paintProcs(QPainter &p, const QRectF &r);
    void paintFooter(QPainter &p);
};
