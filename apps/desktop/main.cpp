#include <QApplication>

#include "puzzle_runner_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("parlawl");
    QApplication::setOrganizationDomain("local.parlawl");
    QApplication::setApplicationName("parlawl");

    PuzzleRunnerWindow window;
    window.show();
    return app.exec();
}
