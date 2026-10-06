#pragma once
#include <QObject>
#include <QUndoStack>
#include "schedule_model.h"

// 撤销/重做管理器：把 ScheduleModel 的调课操作包装成 QUndoCommand
class UndoManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
public:
    explicit UndoManager(ScheduleModel* model, QObject* parent = nullptr);

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }

    // QML 可调用
    Q_INVOKABLE void undo() { m_stack.undo(); }
    Q_INVOKABLE void redo() { m_stack.redo(); }
    Q_INVOKABLE void clear() { m_stack.clear(); }

    // 包装调课（记录前后科目）
    Q_INVOKABLE bool setCell(int row, int col, const QString& newSubjectId);

signals:
    void stateChanged();

private:
    QUndoStack m_stack;
    ScheduleModel* m_model;
};
