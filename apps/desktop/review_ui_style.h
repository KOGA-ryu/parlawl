#pragma once

#include <QString>

namespace parlawl::review_ui {

inline QString calmTabStyleSheet()
{
    return QStringLiteral(
        "QTabWidget::pane { background: #181b20; border: 0; top: -1px; }"
        "QTabBar::tab { background: transparent; color: #929ba7; border: 0; padding: 7px 11px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #252b32; color: #eef2f6; border-radius: 6px; }"
        "QTabBar::tab:!selected:hover { background: #20252b; color: #cbd2db; border-radius: 6px; }");
}

inline QString studyWindowStyleSheet()
{
    return calmTabStyleSheet() + QStringLiteral(
        "QMainWindow { background: #181b20; }"
        "QPushButton { background: #2a2f37; color: #d9dfe7; border: 0; border-radius: 6px; padding: 6px 10px; }"
        "QPushButton:hover { background: #343b45; color: #f4f7fa; }"
        "QPushButton:pressed { background: #20252b; }"
        "QPushButton:disabled { background: #20242a; color: #626b76; }"
        "QComboBox { background: #292f36; color: #e1e6ec; border: 0; border-radius: 6px; padding: 5px 9px; min-width: 92px; }"
        "QComboBox:hover { background: #343b44; }"
        "QPlainTextEdit { background: #14171b; color: #d8dee7; border: 0; border-radius: 8px; padding: 10px; }");
}

} // namespace parlawl::review_ui
