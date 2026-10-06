#include "undo_manager.h"
#include <QUndoCommand>

// 调课命令：记录行列 + 前后科目
class SetCellCommand : public QUndoCommand {
public:
    SetCellCommand(ScheduleModel* model, int row, int col,
                   const QString& oldId, const QString& newId)
        : QUndoCommand(QStringLiteral("调课")), m_model(model),
          m_row(row), m_col(col), m_oldId(oldId), m_newId(newId)
    {}

    void undo() override {
        m_model->setCellSubject(m_row, m_col, m_oldId);
    }
    void redo() override {
        m_model->setCellSubject(m_row, m_col, m_newId);
    }

private:
    ScheduleModel* m_model;
    int m_row, m_col;
    QString m_oldId, m_newId;
};

UndoManager::UndoManager(ScheduleModel* model, QObject* parent)
    : QObject(parent), m_model(model)
{
    connect(&m_stack, &QUndoStack::canUndoChanged, this, &UndoManager::stateChanged);
    connect(&m_stack, &QUndoStack::canRedoChanged, this, &UndoManager::stateChanged);
}

bool UndoManager::setCell(int row, int col, const QString& newSubjectId) {
    if (!m_model) return false;
    if (row < 0 || col < 0) return false;

    // 当前科目（作为撤销的旧值）
    QString oldId = m_model->cellSubjectId(row, col);
    if (oldId == newSubjectId) return false;  // 没变化

    m_stack.push(new SetCellCommand(m_model, row, col, oldId, newSubjectId));
    return true;
}
