#include <QtTest>

#include "ui/dashboard.h"

#include <QImage>
#include <QPixmap>
#include <QSet>

class TestDashboard : public QObject
{
    Q_OBJECT
private slots:
    void rendersDemo();
    void togglesRespond();
    void pausesOnSpace();
    void extremeResize();
    void intervalChangesCapacity();
};

static int countCyan(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            const QColor c = img.pixelColor(x, y);
            if (c.blue() > 180 && c.green() > 140 && c.red() < 160)
                ++n;
        }
    }
    return n;
}

void TestDashboard::rendersDemo()
{
    Dashboard d;
    d.setPaused(true);          // niente timer che sovrascrive la demo
    d.setDemo(true);
    d.resize(1280, 800);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));

    const QPixmap px = d.grab();
    QVERIFY(!px.isNull());
    const QImage img = px.toImage();
    QCOMPARE(img.size(), QSize(1280, 800));

    // immagine non piatta: molti colori distinti
    QSet<QRgb> colors;
    for (int y = 0; y < img.height(); y += 7)
        for (int x = 0; x < img.width(); x += 7)
            colors.insert(img.pixel(x, y));
    QVERIFY2(colors.size() > 60, qPrintable(QStringLiteral("colori: %1").arg(colors.size())));

    // ciano di segnale presente (pannelli + barre dei core)
    const int cyan = countCyan(img);
    QVERIFY2(cyan > 200, qPrintable(QStringLiteral("pixel ciano: %1").arg(cyan)));

    // la demo ha riempito le cronologie: il grafico delle temperature non è vuoto
    const Machine &m = d.machine();
    QVERIFY(m.valid);
    QCOMPARE(m.cores.size(), 20);
    QVERIFY(m.maxTemp() > 30.0);

    // layout: tutte le aree dentro la finestra e non sovrapposte
    const Dashboard::Layout &l = d.layout();
    QVERIFY(l.valid);
    const QRectF all(l.cpu);
    QVERIFY(l.cpu.width() > 0 && l.mem.width() > 0);
    QVERIFY(l.cpu.right() < l.mem.left());
    QVERIFY(l.temp.right() < l.net.left());
    QVERIFY(l.procs.height() > 20);
    QVERIFY(l.procs.bottom() <= l.footer.top() + 0.5);
    QCOMPARE(l.toggles.size(), int(Dashboard::ToggleCount));
    Q_UNUSED(all);
}

void TestDashboard::togglesRespond()
{
    Dashboard d;
    d.resize(1280, 800);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));

    QVERIFY(!d.paused());
    QVERIFY(!d.sortByMem());

    const QRectF pause = d.toggleRect(Dashboard::Pause);
    QVERIFY(pause.isValid());
    QTest::mouseClick(&d, Qt::LeftButton, Qt::NoModifier, pause.center().toPoint());
    QVERIFY(d.paused());

    const QRectF sortMem = d.toggleRect(Dashboard::SortMem);
    QVERIFY(sortMem.isValid());
    QTest::mouseClick(&d, Qt::LeftButton, Qt::NoModifier, sortMem.center().toPoint());
    QVERIFY(d.sortByMem());

    const QRectF demo = d.toggleRect(Dashboard::Demo);
    QVERIFY(demo.isValid());
    QTest::mouseClick(&d, Qt::LeftButton, Qt::NoModifier, demo.center().toPoint());
    QVERIFY(d.demo());

    // un click fuori dagli interruttori non cambia nulla
    QTest::mouseClick(&d, Qt::LeftButton, Qt::NoModifier, QPoint(40, 300));
    QVERIFY(d.paused() && d.sortByMem() && d.demo());
}

void TestDashboard::pausesOnSpace()
{
    Dashboard d;
    d.setPaused(false);
    d.resize(960, 640);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    d.setFocus();

    QVERIFY(!d.paused());
    QTest::keyClick(&d, Qt::Key_Space);
    QVERIFY(d.paused());
    QTest::keyClick(&d, Qt::Key_Space);
    QVERIFY(!d.paused());
    d.setPaused(true);
}

void TestDashboard::extremeResize()
{
    Dashboard d;
    d.setPaused(true);
    d.setDemo(true);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));

    d.resize(360, 260);
    const QSize minimal = d.size();
    const QPixmap small = d.grab();
    QVERIFY(!small.isNull());
    QVERIFY(d.layout().valid);
    QVERIFY2(d.layout().toggles.size() == int(Dashboard::ToggleCount), "gli interruttori spariscono");

    d.resize(2400, 1400);
    const QPixmap big = d.grab();
    QVERIFY(!big.isNull());
    QVERIFY(big.width() >= 2000);
    QVERIFY(d.toggleRect(Dashboard::Demo).isValid());
    Q_UNUSED(minimal);
}

void TestDashboard::intervalChangesCapacity()
{
    Dashboard d;
    QCOMPARE(d.intervalMs(), 200);            // 5 volte al secondo
    QCOMPARE(d.windowSec(), 60);              // un minuto di storico
    QCOMPARE(d.capacity(), 300);

    d.setIntervalMs(1000);
    QCOMPARE(d.capacity(), 60);

    d.setIntervalMs(40);                      // 25 volte al secondo
    QCOMPARE(d.capacity(), 1500);

    d.setIntervalMs(5);                       // sotto il minimo: si ferma a 20 ms (50 Hz)
    QCOMPARE(d.intervalMs(), 20);
    QCOMPARE(d.capacity(), 3000);

    d.setWindowSec(180);                      // finestra più lunga = scorre più piano
    QCOMPARE(d.capacity(), 9000);
    d.setWindowSec(30);
    QCOMPARE(d.capacity(), 1500);
    d.setWindowSec(1);                        // minimo 5 s
    QCOMPARE(d.windowSec(), 5);
}

QTEST_MAIN(TestDashboard)
#include "test_dashboard.moc"
