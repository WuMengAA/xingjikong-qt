#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QDateTime>
#include "profile.h"
#include "profile_repository.h"
#include "schedule_model.h"
#include "timeslot_model.h"
#include "subject_model.h"
#include "cses_importer.h"
#include "undo_manager.h"

// 从命令行参数读取档案路径
static QString getProfilePathFromArgs(const QStringList& args) {
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--profile" && i + 1 < args.size()) {
            return args[i + 1];
        }
    }
    return {};
}

// 创建示例档案
static Profile createSampleProfile() {
    Profile p;
    p.name = QStringLiteral("示例档案");
    p.id = QUuid::createUuid().toString();
    p.isActive = true;

    TimeSlot ts1;
    ts1.id = QUuid::createUuid().toString();
    ts1.name = QStringLiteral("第一节");
    ts1.startTime = QTime(8, 0);
    ts1.endTime = QTime(8, 45);
    ts1.timeType = 0;
    ts1.isActive = true;
    p.timeSlots[ts1.id] = ts1;

    TimeSlot ts2;
    ts2.id = QUuid::createUuid().toString();
    ts2.name = QStringLiteral("第二节");
    ts2.startTime = QTime(8, 55);
    ts2.endTime = QTime(9, 40);
    ts2.timeType = 0;
    ts2.isActive = true;
    p.timeSlots[ts2.id] = ts2;

    TimeSlot ts3;
    ts3.id = QUuid::createUuid().toString();
    ts3.name = QStringLiteral("第三节");
    ts3.startTime = QTime(10, 10);
    ts3.endTime = QTime(10, 55);
    ts3.timeType = 0;
    ts3.isActive = true;
    p.timeSlots[ts3.id] = ts3;

    Subject sub1;
    sub1.id = QUuid::createUuid().toString();
    sub1.name = QStringLiteral("语文");
    sub1.simplifiedName = QStringLiteral("语");
    p.subjects[sub1.id] = sub1;

    Subject sub2;
    sub2.id = QUuid::createUuid().toString();
    sub2.name = QStringLiteral("数学");
    sub2.simplifiedName = QStringLiteral("数");
    p.subjects[sub2.id] = sub2;

    Subject sub3;
    sub3.id = QUuid::createUuid().toString();
    sub3.name = QStringLiteral("英语");
    sub3.simplifiedName = QStringLiteral("英");
    p.subjects[sub3.id] = sub3;

    ClassPlanGroup grp;
    grp.id = QUuid::createUuid().toString();
    grp.name = QStringLiteral("默认课表");
    grp.isActive = true;
    p.classPlanGroups[grp.id] = grp;

    ClassPlan cp1;
    cp1.id = QUuid::createUuid().toString();
    cp1.name = QStringLiteral("周一");
    cp1.weekDay = 1;
    cp1.isActive = true;
    cp1.lessons.append(Lesson{sub1.id, 1, 0, 1, 1, true});
    cp1.lessons.append(Lesson{sub2.id, 1, 1, 1, 1, true});
    cp1.lessons.append(Lesson{sub3.id, 1, 2, 1, 1, true});
    p.classPlans[cp1.id] = cp1;
    grp.classPlanIds.append(cp1.id);

    ClassPlan cp2;
    cp2.id = QUuid::createUuid().toString();
    cp2.name = QStringLiteral("周二");
    cp2.weekDay = 2;
    cp2.isActive = true;
    cp2.lessons.append(Lesson{sub2.id, 2, 0, 1, 1, true});
    cp2.lessons.append(Lesson{sub3.id, 2, 1, 1, 1, true});
    cp2.lessons.append(Lesson{sub1.id, 2, 2, 1, 1, true});
    p.classPlans[cp2.id] = cp2;
    grp.classPlanIds.append(cp2.id);

    p.classPlanGroups[grp.id] = grp;
    p.selectedClassPlanGroupId = grp.id;

    // 第二套课表群：模拟双周轮换（周一到周三用另一组课表）
    ClassPlanGroup grp2;
    grp2.id = QUuid::createUuid().toString();
    grp2.name = QStringLiteral("双周轮换");
    grp2.isActive = true;

    ClassPlan cp3;
    cp3.id = QUuid::createUuid().toString();
    cp3.name = QStringLiteral("周一·双周");
    cp3.weekDay = 1;
    cp3.weekCountDiv = 2;
    cp3.weekCountDivTotal = 2;
    cp3.isActive = true;
    cp3.lessons.append(Lesson{sub3.id, 1, 0, 2, 2, true});  // 英语
    cp3.lessons.append(Lesson{sub1.id, 1, 1, 2, 2, true});  // 语文
    cp3.lessons.append(Lesson{sub2.id, 1, 2, 2, 2, true});  // 数学
    p.classPlans[cp3.id] = cp3;
    grp2.classPlanIds.append(cp3.id);

    ClassPlan cp4;
    cp4.id = QUuid::createUuid().toString();
    cp4.name = QStringLiteral("周二·双周");
    cp4.weekDay = 2;
    cp4.weekCountDiv = 2;
    cp4.weekCountDivTotal = 2;
    cp4.isActive = true;
    cp4.lessons.append(Lesson{sub1.id, 2, 0, 2, 2, true});
    cp4.lessons.append(Lesson{sub2.id, 2, 1, 2, 2, true});
    cp4.lessons.append(Lesson{sub3.id, 2, 2, 2, 2, true});
    p.classPlans[cp4.id] = cp4;
    grp2.classPlanIds.append(cp4.id);

    p.classPlanGroups[grp2.id] = grp2;

    return p;
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Stelarith"));
    app.setApplicationName(QStringLiteral("schedule-qt"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // 加载档案
    ProfileRepository repo;
    const QString profilePath = getProfilePathFromArgs(app.arguments());
    Profile profile;

    if (!profilePath.isEmpty()) {
        profile = repo.load(profilePath);
        qInfo() << "Loaded profile from:" << profilePath;
    } else {
        profile = repo.loadDefault();
        qInfo() << "Loaded default profile from:" << repo.defaultProfilePath();
    }

    if (profile.classPlans.isEmpty()) {
        qInfo() << "Profile is empty, creating sample data";
        profile = createSampleProfile();
    }

    // 创建 Models（QObject 子类，可直接注册到 QML）
    ScheduleModel scheduleModel(profile);
    scheduleModel.setCurrentWeek(ScheduleModel::weekFromDate(QDate::currentDate()));
    TimeSlotModel timeSlotModel;
    {
        QList<TimeSlot> active;
        for (const auto& ts : profile.timeSlots) {
            if (ts.isActive) active.append(ts);
        }
        timeSlotModel.setSlots(active);
    }
    SubjectModel subjectModel(profile.subjects);
    subjectModel.setProfileRef(&profile);

    // 撤销/重做
    UndoManager undoManager(&scheduleModel);

    // QML 上下文（直接传指针，QML 通过 context property 访问）
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("scheduleModel"), &scheduleModel);
    engine.rootContext()->setContextProperty(QStringLiteral("timeSlotModel"), &timeSlotModel);
    engine.rootContext()->setContextProperty(QStringLiteral("subjectModel"), &subjectModel);
    engine.rootContext()->setContextProperty(QStringLiteral("undoManager"), &undoManager);
    engine.rootContext()->setContextProperty(QStringLiteral("profileName"), profile.name);
    engine.rootContext()->setContextProperty(QStringLiteral("profilePath"), profilePath.isEmpty() ? repo.defaultProfilePath() : profilePath);

    // 加载主窗口（传统 qrc 资源：qrc:/qml/qml/Main.qml，见 build/.qt/rcc/qml_res.qrc）
    // QML 警告/错误实时转发到 stderr（GUI 程序无控制台，但 stderr 可重定向到文件）
    QObject::connect(&engine, &QQmlEngine::warnings,
                     [](const QList<QQmlError>& warnings) {
                         for (const QQmlError& e : warnings) {
                             QTextStream(stderr) << "QML: " << e.toString() << "\n";
                         }
                     });
    engine.load(QUrl(QStringLiteral("qrc:/qml/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        QTextStream(stderr) << "FATAL: QML failed to load\n";
        return -1;
    }

    return app.exec();
}
