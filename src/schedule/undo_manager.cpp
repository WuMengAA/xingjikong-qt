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

    void undo() override { m_model->setCellSubject(m_row, m_col, m_oldId); }
    void redo() override { m_model->setCellSubject(m_row, m_col, m_newId); }

private:
    ScheduleModel* m_model;
    int m_row, m_col;
    QString m_oldId, m_newId;
};

// 时间轴拖拽命令：记录该时间段的起止分钟（前后）
class SetSlotTimeCommand : public QUndoCommand {
public:
    SetSlotTimeCommand(TimeSlotModel* model, int row,
                       int oldStart, int oldEnd, int newStart, int newEnd)
        : QUndoCommand(QStringLiteral("调整时间轴")), m_model(model),
          m_row(row), m_oldStart(oldStart), m_oldEnd(oldEnd),
          m_newStart(newStart), m_newEnd(newEnd)
    {}

    void undo() override { m_model->setSlotByMinutes(m_row, m_oldStart, m_oldEnd); }
    void redo() override { m_model->setSlotByMinutes(m_row, m_newStart, m_newEnd); }

private:
    TimeSlotModel* m_model;
    int m_row;
    int m_oldStart, m_oldEnd, m_newStart, m_newEnd;
};

// 科目新增命令
class AddSubjectCommand : public QUndoCommand {
public:
    AddSubjectCommand(SubjectModel* model, const QString& name, const QString& simplifiedName)
        : QUndoCommand(QStringLiteral("新增科目")), m_model(model),
          m_name(name), m_short(simplifiedName) {}

    void undo() override {
        const QString id = m_model->idAt(m_row);
        if (!id.isEmpty()) m_model->removeAt(m_row);
        if (m_row > 0) --m_row;
    }
    void redo() override {
        m_row = m_model->addSubjectQml2(m_name, m_short);
    }

private:
    SubjectModel* m_model;
    QString m_name, m_short;
    int m_row = -1;
};

// 科目删除命令
class RemoveSubjectCommand : public QUndoCommand {
public:
    RemoveSubjectCommand(SubjectModel* model, int row,
                         const QString& id, const QString& name,
                         const QString& shortName)
        : QUndoCommand(QStringLiteral("删除科目")), m_model(model),
          m_row(row), m_id(id), m_name(name), m_short(shortName) {}

    void undo() override { m_model->insertAt(m_row, m_id, m_name, m_short); }
    void redo() override { m_model->removeAt(m_row); }

private:
    SubjectModel* m_model;
    int m_row;
    QString m_id, m_name, m_short;
};

// 科目改名命令
class RenameSubjectCommand : public QUndoCommand {
public:
    RenameSubjectCommand(SubjectModel* model, int row,
                         const QString& oldName, const QString& oldShort,
                         const QString& newName, const QString& newShort)
        : QUndoCommand(QStringLiteral("重命名科目")), m_model(model),
          m_row(row), m_oldName(oldName), m_oldShort(oldShort),
          m_newName(newName), m_newShort(newShort) {}

    void undo() override { m_model->renameAt2(m_row, m_oldName, m_oldShort); }
    void redo() override { m_model->renameAt2(m_row, m_newName, m_newShort); }

private:
    SubjectModel* m_model;
    int m_row;
    QString m_oldName, m_oldShort, m_newName, m_newShort;
};

UndoManager::UndoManager(ScheduleModel* model, TimeSlotModel* slotModel,
                         SubjectModel* subjectModel, QObject* parent)
    : QObject(parent), m_model(model), m_slotModel(slotModel), m_subjectModel(subjectModel)
{
    connect(&m_stack, &QUndoStack::canUndoChanged, this, &UndoManager::stateChanged);
    connect(&m_stack, &QUndoStack::canRedoChanged, this, &UndoManager::stateChanged);
}

bool UndoManager::setCell(int row, int col, const QString& newSubjectId) {
    if (!m_model || row < 0 || col < 0) return false;
    const QString oldId = m_model->cellSubjectId(row, col);
    if (oldId == newSubjectId) return false;
    m_stack.push(new SetCellCommand(m_model, row, col, oldId, newSubjectId));
    return true;
}

bool UndoManager::setSlotTime(int row, int startMin, int endMin) {
    if (!m_slotModel || row < 0) return false;
    const int oldStart = m_slotModel->startMinAt(row);
    const int oldEnd = m_slotModel->endMinAt(row);
    if (oldStart == startMin && oldEnd == endMin) return false;
    m_stack.push(new SetSlotTimeCommand(m_slotModel, row, oldStart, oldEnd, startMin, endMin));
    return true;
}

bool UndoManager::addSubject(const QString& name, const QString& simplifiedName) {
    if (!m_subjectModel || name.trimmed().isEmpty()) return false;
    m_stack.push(new AddSubjectCommand(m_subjectModel, name.trimmed(), simplifiedName.trimmed()));
    return true;
}

bool UndoManager::removeSubject(const QString& id) {
    if (!m_subjectModel) return false;
    const int row = m_subjectModel->indexOfId(id);
    if (row < 0) return false;
    m_stack.push(new RemoveSubjectCommand(m_subjectModel, row, id,
                                          m_subjectModel->nameAt(row),
                                          m_subjectModel->simplifiedNameAt(row)));
    return true;
}

bool UndoManager::renameSubject(const QString& id, const QString& newName,
                                const QString& newSimplifiedName) {
    if (!m_subjectModel) return false;
    const int row = m_subjectModel->indexOfId(id);
    if (row < 0) return false;
    m_stack.push(new RenameSubjectCommand(m_subjectModel, row,
                                          m_subjectModel->nameAt(row),
                                          m_subjectModel->simplifiedNameAt(row),
                                          newName.trimmed(), newSimplifiedName.trimmed()));
    return true;
}
