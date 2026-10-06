#pragma once
#include <QObject>
#include <QUndoStack>
#include "schedule_model.h"
#include "timeslot_model.h"
#include "subject_model.h"

// 撤销/重做管理器：把 ScheduleModel 的调课、TimeSlotModel 的时间轴拖拽、
// SubjectModel 的科目增删改全部包装成 QUndoCommand —— 一个栈统一撤销。
class UndoManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
public:
    explicit UndoManager(ScheduleModel* model, TimeSlotModel* slotModel,
                         SubjectModel* subjectModel, QObject* parent = nullptr);

    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }

    // QML 可调用
    Q_INVOKABLE void undo() { m_stack.undo(); }
    Q_INVOKABLE void redo() { m_stack.redo(); }
    Q_INVOKABLE void clear() { m_stack.clear(); }

    // 包装调课（记录前后科目）
    Q_INVOKABLE bool setCell(int row, int col, const QString& newSubjectId);
    // 时间轴拖拽：改某时间段的起止分钟（吸附后）
    Q_INVOKABLE bool setSlotTime(int row, int startMin, int endMin);
    // 科目 CRUD
    Q_INVOKABLE bool addSubject(const QString& name, const QString& simplifiedName);
    Q_INVOKABLE bool removeSubject(const QString& id);
    Q_INVOKABLE bool renameSubject(const QString& id, const QString& newName,
                                   const QString& newSimplifiedName);

signals:
    void stateChanged();

private:
    QUndoStack m_stack;
    ScheduleModel* m_model;
    TimeSlotModel* m_slotModel;
    SubjectModel* m_subjectModel;
};
