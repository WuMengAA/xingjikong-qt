#ifndef CLASS_ADMIN_H
#define CLASS_ADMIN_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <functional>

class ViewerBackend;

// 班级管理调用层（2026-10-24 · 管理端「班级管理」面板 · 方案 B）
//
// 四个写操作全部走 HTTP POST /api/class/*（云端 src/class-admin.js）：
//   create 只给班名即建班（不绑设备）/ rename 只改显示名（canonical 码不动）/
//   delete 带策略（refuse 有设备即拒 / release 先移出再删）/ unbind 移出一台设备。
//
// 与 ClassService 的分工（刻意不混）：ClassService 是 WS 班级域**数据面**
// （订阅/快照/事件/缩略图，零 HTTP）；本类只发**管理写操作**，不持有任何
// 班级状态 —— 班级名单唯一真源是云端 /api/classes（写成功后 QML 立即重拉，
// 不等 15s 轮询）。凭据不外泄：每次 POST 现取 backend->httpBaseUrl() /
// viewerToken()（与 InteractBridge 同一接口，抄两份必然漂）。
class ClassAdmin : public QObject
{
    Q_OBJECT
public:
    explicit ClassAdmin(ViewerBackend *backend, QObject *parent = nullptr);

    Q_INVOKABLE void createClass(const QString &className);
    Q_INVOKABLE void renameClass(const QString &code, const QString &name);
    Q_INVOKABLE void deleteClass(const QString &code, const QString &strategy);
    Q_INVOKABLE void unbindDevice(const QString &code, const QString &uid);
    Q_INVOKABLE void bindDevice(const QString &code, const QString &uid);
    // ── 班级成员（人可以被添加，2026-10-24）──
    // members() 是同步取一次成员清单（拉完发 membersList 信号，QML 填表）；
    // addMember/removeMember 是写操作，反馈走 opFinished（成功文案带成员账号）。
    Q_INVOKABLE void members(const QString &code);
    Q_INVOKABLE void addMember(const QString &code, const QString &uid, const QString &role);
    Q_INVOKABLE void removeMember(const QString &code, const QString &uid);

    // ── 发布中心（P1 第一刀：通知三件套 + 作业）──
    // publish 走 POST /api/class/publish，body { classCode, card:{type,title,body} }。
    // type ∈ {student-notice, class-notice, fullscreen-notice, homework}（云端白名单，
    // 非法类型/空内容/无权限 都回 opFinished(false, 人话)）。成功文案带送达/在线台数。
    Q_INVOKABLE void publish(const QString &classCode, const QString &type,
                             const QString &title, const QString &body);

    // ── 学生名单（2026-10-25 · 云端数据库，用户裁决"不要本机了"）──
    // 与「成员」（老师/助教账号）是两回事：学生不是登录账号，是班级事务的对象。
    // students() 拉一次名单发 studentsList；写操作走 opFinished（成功文案带姓名）。
    Q_INVOKABLE void students(const QString &code);
    Q_INVOKABLE void addStudent(const QString &code, const QString &name,
                                const QString &studentNo, const QString &seat);
    Q_INVOKABLE void updateStudent(const QString &code, const QString &studentId,
                                   const QString &name, const QString &studentNo,
                                   const QString &seat, const QString &note);
    Q_INVOKABLE void removeStudent(const QString &code, const QString &studentId);

    // ── 班级课表（同批：整份 profile 存云端，不再只存在本机）──
    // timetable() 拉一次发 timetableLoaded（profile 无效 = 这个班还没排课，
    // 界面必须显示"还没排课"而不是空白表）；saveTimetable 写整份档案。
    Q_INVOKABLE void timetable(const QString &code);
    Q_INVOKABLE void saveTimetable(const QString &code, const QString &profileJson);

    // ── ClassIsland 档案导入（2026-10-25 · CIMS 退役后课表回落到云端）──
    // 解析规则单点在云端 src/class-timetable.js（第 N 节课 ↔ 作息第 N 个 type=0 时段），
    // 客户端只上传**原始档案文本**，不再各写一份解析（两套口径必然漂）。
    // 档案常含多个课表群（一个年级多个班）：先 timetableGroups() 预览，让老师选，
    // 绝不猜 —— 猜错就是把 8 班课表挂到 1 班。
    Q_INVOKABLE void importTimetable(const QString &code, const QString &profileJson,
                                     const QString &group);
    /** 只预览档案里的课表群（不落库），结果走 timetableGroupsList。 */
    Q_INVOKABLE void timetableGroups(const QString &profileJson);

    // ── 课表下发到教室机（2026-11-02）──
    // 改造前课表只进云端库、只广播给管理端浏览器，**教室机一无所知**（它读本机
    // ClassIsland 档案）——两端各自正确、中间从未接线。这两件把下发与送达视图补上：
    //   pushTimetable → opFinished("timetable-push", …)，文案带「送达 N / 在线 M」
    //   timetableDelivery → timetableDeliveryList（已落盘 / 离线没回执必须分开显示）
    Q_INVOKABLE void pushTimetable(const QString &code);
    Q_INVOKABLE void timetableDelivery(const QString &code);

    // ── 学生名单批量导入（CSV 在 QML 侧解析成 rows；云端按学号幂等 upsert）──
    Q_INVOKABLE void importStudents(const QString &code, const QVariantList &rows);
    /** 考勤（2026-10-31）：拉某班某日的考勤记录 + 四档统计（GET /api/class/attendance）。 */
    Q_INVOKABLE void attendance(const QString &code, const QString &date);
    /** 考勤写入（2026-10-31）：rows = [{studentNo, status}]（upsert 幂等）。 */
    Q_INVOKABLE void saveAttendance(const QString &code, const QString &date,
                                    const QString &session, const QVariantList &rows);

    /** 读本地文本文件（选课表档案 / CSV 用）。
     *  为什么放在这里而不是 backend：班级域自包含（Android 上 pickFile 是异步的，
     *  拿不到同步路径就返回空串，由调用方提示"该端暂不支持导入"）。 */
    Q_INVOKABLE QString readTextFile(const QString &path, int maxBytes = 4 * 1024 * 1024);
    Q_INVOKABLE QString lastFileError() const { return m_fileError; }

    /** 判断一段文本是不是 ClassIsland 档案：必须**同时**含 ClassPlans 与 TimeLayouts。
     *  2026-10-29：原先 QML 自己 JSON.parse 再判 —— 既违反 §10.9（QML 禁 JSON.parse），
     *  又让"什么算合法档案"的口径散在两端。收归 C++：QML 只拿一个布尔值。 */
    Q_INVOKABLE bool isClassIslandArchive(const QString &text);

    // ── 班级接入码（2026-10-25 · 收归我方后班级管理面板的入口码）──
    // 用户裁决：「班级接入码收归我方管理，作为班级管理面板的入口码，所以去掉 CIMS
    // 时期的旧方式」。语义：**设备凭这串码进入这个班**（一次性、绑班、24h）。
    //
    // issueBindCode 走 POST /api/class/bindcode（班级域，与上面八件同一套鉴权）：
    //   成功 → bindCodeIssued（码 + 有效期，QML 直接"把这串给老师"）
    //   失败 → opFinished("bindcode", false, 云端人话)
    // revokeBindCode 走 bindcode-revoke：发错班了就撤回，TTL 内也立刻失效。
    Q_INVOKABLE void issueBindCode(const QString &classCode);
    Q_INVOKABLE void revokeBindCode(const QString &code);

signals:
    /** 操作完成。ok=false 时 message 是云端给的人话错误（网络层失败也有兜底文案）。 */
    void opFinished(const QString &op, bool ok, const QString &message);
    /** 一个班的学生名单（students() 结果；空数组 = 还没录入）。 */
    void studentsList(const QString &classCode, const QVariantList &students);
    /** 一个班的考勤（attendance() 结果：records=[{studentNo,studentName,status}]，
     *  stats={counts:{present,late,leave,absent}, recorded, rosterSize}）。 */
    void attendanceList(const QString &classCode, const QString &date,
                        const QVariantList &records, const QVariantMap &stats);
    /** 一个班的课表档案。2026-10-29：profile 直接是**结构化 QVariant**
     *  （对象→QVariantMap / 数组→QVariantList；无效 QVariant = 还没排课），
     *  QML 拿到即可用，**不再 JSON.parse**（§10.9 QML 纯净封杀令）。
     *  updatedAt = 毫秒时间戳。 */
    void timetableLoaded(const QString &classCode, const QVariant &profile, qint64 updatedAt);
    /** 一个班的成员清单取回（members() 的结果；空数组 = 班里没人）。 */
    void membersList(const QString &classCode, const QVariantList &members);
    /** 一枚新生成的班级接入码（2026-10-25）。
     *  expiresAt = 毫秒时间戳；ttlSec = 剩余秒数（云端算好，免得两端时钟漂移）；
     *  className 可能为空串（班没改过名时云端给 code 兜底），UI 显示要能容错。 */
    void bindCodeIssued(const QString &classCode, const QString &className,
                        const QString &code, qint64 expiresAt, int ttlSec);
    /** 2026-10-24 重认证门：写操作被云端 REAUTH_REQUIRED 拦（403）。
     *  此时连接/会话都还在，只是"该再验一次密码了"。UI 收到后弹提示 →
     *  走一次重新登录（backend.loginWithSite()）→ 新票到位后重试即可。 */
    void reauthRequired(const QString &message);
    /** 档案里的课表群清单（importTimetable/timetableGroups 的答复）。
     *  item = { id, name, days }；空列表 = 档案里没有可用日课表。 */
    void timetableGroupsList(const QVariantList &groups);
    /** 课表送达视图（timetableDelivery 的答复）。
     *  delivered = [{uid, ok, changed, days, lessons, error, at, classCode}]，
     *  pending = [uid…]（**从没回过执**）。
     *  两者必须分开显示：「没收到」要人去现场，「收到但失败」是另一条处置路径。 */
    void timetableDeliveryList(const QString &classCode, const QVariantList &delivered,
                               const QVariantList &pending);

private:
    /** okText 非空时用作成功回执文案（学生操作要能说清"加了谁"，不能说"高二3班"）。 */
    void post(const QString &action, const QJsonObject &body, const QString &okText = QString());
    /** 需要读响应字段的写操作（课表导入 / 学生批量导入）走这条：
     *  成功时回调拿到响应 JSON（失败一律走 opFinished / reauthRequired，不重复回调）。 */
    void postJson(const QString &action, const QJsonObject &body,
                  std::function<void(const QJsonObject &)> done);
    /** 取 JSON 列表类接口（students/timetable 共用）：成功走 onOk，失败静默发空。 */
    void fetchJson(const QString &action, const QJsonObject &body,
                   std::function<void(const QJsonObject &)> onOk);

    ViewerBackend *m_backend = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_fileError;      ///< readTextFile 的失败原因（空 = 上次读成功）
};

#endif // CLASS_ADMIN_H
