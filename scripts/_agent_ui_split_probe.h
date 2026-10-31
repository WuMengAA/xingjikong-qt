#pragma once
// 拆分可行性实验用到的两个类（拆成独立头是 moc 的要求：
// moc 产出的 _moc.cpp 需要能单独看到类声明，否则报 "RecordVm 未声明的标识符"）。

#include <QObject>
#include <QString>

// ── 基类：模拟要搬出去的 agent_record_ui.h ─────────────────────────────
class RecordVm : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString recordSummary READ recordSummary NOTIFY infoChanged)
public:
    QString recordSummary() const { return m_summary; }
    void setSummary(const QString &s) { m_summary = s; }
signals:
    void infoChanged();
private:
    QString m_summary = QStringLiteral("init");
};

// ── 派生类：模拟 agent_ui.h ───────────────────────────────────────────
class AgentUiLike : public RecordVm
{
    Q_OBJECT
    Q_PROPERTY(QString ownValue READ ownValue NOTIFY infoChanged)
public:
    QString ownValue() const { return m_own; }
    void setOwn(const QString &s) { m_own = s; }

    // main.cpp 现在调的是 ui->publish()
    void publishDerivedOnly() { emit infoChanged(); }                                 // 只发派生类
    void publishBoth()        { emit infoChanged(); emit RecordVm::infoChanged(); }   // 两个都发
signals:
    void infoChanged();
private:
    QString m_own = QStringLiteral("own-init");
};
