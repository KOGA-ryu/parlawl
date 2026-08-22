#pragma once

// STUDY: write the whole line first, then step the reveal bar by bar.
//
// Study mode does not weaken the vault. The only differences from rush are the
// absence of a deadline and the presence of post-reveal navigation. There is no
// peek, no "just show me", no setting.

#include <QVector>
#include <QWidget>

#include "market_session_controller.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QTextEdit;
QT_END_NAMESPACE

class MarketStudyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MarketStudyPanel(QWidget *parent = nullptr);

    void setController(parlawl::market::MarketSessionController *controller);
    void refresh();

    [[nodiscard]] QString promptText() const;
    [[nodiscard]] QString revealSummaryText() const;
    [[nodiscard]] int planEditorRowCount() const { return m_editors.size(); }
    [[nodiscard]] bool canCommitPlan() const;
    [[nodiscard]] bool canStepContinuation() const;

signals:
    void revealChanged();

private:
    struct PlyEditor
    {
        parlawl::market::MarketPlySpec spec;
        QComboBox *primary = nullptr;
        QComboBox *secondary = nullptr;
        QDoubleSpinBox *probability = nullptr;
    };

    void rebuildEditor();
    void commitPlan();
    void submitCalibration();
    void openReveal();
    void stepContinuation();
    void renderReveal();

    parlawl::market::MarketSessionController *m_controller = nullptr;
    QFormLayout *m_planForm;
    QWidget *m_planContainer;
    QLabel *m_promptLabel;
    QLabel *m_errorLabel;
    QPushButton *m_commitButton;
    QDoubleSpinBox *m_lowerSpin;
    QDoubleSpinBox *m_upperSpin;
    QPushButton *m_submitIntervalButton;
    QPushButton *m_revealButton;
    QPushButton *m_stepButton;
    QTextEdit *m_revealView;
    QVector<PlyEditor> m_editors;
    QString m_editorPuzzleId;
};
