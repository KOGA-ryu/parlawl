#include <QtTest/QtTest>

#include "pgn_utils.h"

class PgnUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void extractsMoveListAndOpening();
    void fallsBackToEcoWhenOpeningHeaderMissing();
};

void PgnUtilsTest::extractsMoveListAndOpening()
{
    const QString pgn =
        "[Event \"Test\"]\n"
        "[Opening \"Italian Game\"]\n"
        "\n"
        "1. e4 e5 2. Nf3 Nc6 3. Bc4 {idea} Bc5 4. c3 Nf6 5. d4 exd4 1-0\n";

    QCOMPARE(parlawl::puzzle_runner::pgnOpeningName(pgn), QStringLiteral("Italian Game"));
    QCOMPARE(parlawl::puzzle_runner::pgnEcoCode(pgn), QStringLiteral(""));
    QCOMPARE(parlawl::puzzle_runner::pgnDisplayOpening(pgn), QStringLiteral("Italian Game"));
    QCOMPARE(parlawl::puzzle_runner::pgnMoveList(pgn),
             QStringList({QStringLiteral("e4"), QStringLiteral("e5"), QStringLiteral("Nf3"), QStringLiteral("Nc6"),
                          QStringLiteral("Bc4"), QStringLiteral("Bc5"), QStringLiteral("c3"), QStringLiteral("Nf6"),
                          QStringLiteral("d4"), QStringLiteral("exd4")}));
}

void PgnUtilsTest::fallsBackToEcoWhenOpeningHeaderMissing()
{
    const QString pgn =
        "[Event \"Test\"]\n"
        "[ECO \"C50\"]\n"
        "\n"
        "1. e4 e5 2. Nf3 Nc6\n";

    QCOMPARE(parlawl::puzzle_runner::pgnOpeningName(pgn), QStringLiteral(""));
    QCOMPARE(parlawl::puzzle_runner::pgnEcoCode(pgn), QStringLiteral("C50"));
    QCOMPARE(parlawl::puzzle_runner::pgnDisplayOpening(pgn), QStringLiteral("C50"));
}

QTEST_MAIN(PgnUtilsTest)

#include "test_unit_pgn_utils.moc"
