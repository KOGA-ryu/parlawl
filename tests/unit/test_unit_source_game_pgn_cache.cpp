#include <QtTest/QtTest>

#include <QDir>
#include <QStandardPaths>

#include "source_game_pgn_cache.h"

class SourceGamePgnCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void storesAndLoadsPgn();
};

void SourceGamePgnCacheTest::storesAndLoadsPgn()
{
    SourceGamePgnCache cache;
    QString errorMessage;
    QVERIFY2(cache.store(QStringLiteral("game_test_cache"), QStringLiteral("[Event \"X\"]\n\n1. e4 e5 *"), &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));

    const QString loaded = cache.load(QStringLiteral("game_test_cache"), &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(loaded.contains(QStringLiteral("1. e4 e5")));
}

QTEST_MAIN(SourceGamePgnCacheTest)

#include "test_unit_source_game_pgn_cache.moc"
