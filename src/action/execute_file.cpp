// 星集控被控端 · 文件接收指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：分片接收（file_chunk）/ 收尾校验（file_done）/ 发起接收（file_push）。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"

ExecOut executeFileAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // 接收文件的一个分片。params: seq（第几片，从 0）+ data（base64）
    // 必须按 seq 顺序到达才追加，乱序如实报错（不猜顺序，也不静默丢弃）。
    if (action == QStringLiteral("file_chunk")) {
        handled = true;
        if ((*g_actionDeps->fileRecvTarget).isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 没有打开的接收会话，请先下发 file_push");
            return out;
        }
        const int seq = params.value(QStringLiteral("seq")).toInt(-1);
        if (seq < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: seq 非法");
            return out;
        }
        if (seq != (*g_actionDeps->fileRecvNext)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 期望第 %1 片，收到第 %2 片（乱序，拒绝）")
                            .arg((*g_actionDeps->fileRecvNext)).arg(seq);
            return out;
        }
        const QByteArray data = QByteArray::fromBase64(
            params.value(QStringLiteral("data")).toString().toLatin1());
        if (data.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: data 为空（base64 解码后 0 字节）");
            return out;
        }
        if (!(*g_actionDeps->fileRecvFile) || !(*g_actionDeps->fileRecvFile)->isOpen()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 接收文件句柄已关闭");
            return out;
        }
        const qint64 written = (*g_actionDeps->fileRecvFile)->write(data);
        if (written != data.size()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_chunk: 写入失败（写入 %1 / 期望 %2 字节）")
                            .arg(written).arg(data.size());
            return out;
        }
        (*g_actionDeps->fileRecvBytes) += data.size();
        (*g_actionDeps->fileRecvNext)++;
        (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();   // 会话还在动，别清理
        // #198（2026-10-11）：每收一片就推一次进度到灵动岛（分母用声明总字节）。
        IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), (*g_actionDeps->fileRecvBytes), (*g_actionDeps->fileRecvTotal));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("seq"), seq);
        out.data.insert(QStringLiteral("bytes"), written);
        out.data.insert(QStringLiteral("totalBytes"), (*g_actionDeps->fileRecvBytes));
        return out;
    }

    // 结束接收：校验接收到的字节数与声明的 total_bytes 一致，否则视为损坏。
    if (action == QStringLiteral("file_done")) {
        handled = true;
        if ((*g_actionDeps->fileRecvTarget).isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error =QStringLiteral("file_done: 当前没有正在进行的接收会话");
            return out;
        }
        if ((*g_actionDeps->fileRecvFile)) {
            (*g_actionDeps->fileRecvFile)->flush();
            (*g_actionDeps->fileRecvFile)->close();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
        }
        const qint64 expect = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        // 取消的判据改看**显式标志**，不再拿 total_bytes<=0 顶替：
        // 那样做等于把"0 字节文件"和"取消"焊成同一件事，一个真要推的空文件会被当成取消删掉。
        // 老管理端不会带 cancel 字段，那时仍按 total_bytes<=0 认取消 —— 兼容不变。
        const bool wantCancel = params.value(QStringLiteral("cancel")).toBool(false)
                                || (!params.contains(QStringLiteral("cancel")) && expect <= 0);
        if (wantCancel) {
            // 发端点了"取消"。半截文件**必须删掉**：留着就是这台机器上打不开的残缺文件，
            // 中间这段时间谁点开谁中招。
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("cancelled"), true);
            out.data.insert(QStringLiteral("bytes"), (*g_actionDeps->fileRecvBytes));
            out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget)));
            QFile::remove((*g_actionDeps->fileRecvTarget));
            (*g_actionDeps->fileRecvTarget).clear();
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvLastMs) = 0;
            IslandOverlay::instance()->clearFileTransfer();   // #198：取消 ⇒ 进度卡撤掉
            return out;
        }
        if (expect != (*g_actionDeps->fileRecvBytes)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_done: 字节数不符（收到 %1 / 声明 %2），文件已删除")
                            .arg((*g_actionDeps->fileRecvBytes)).arg(expect);
            QFile::remove((*g_actionDeps->fileRecvTarget));
            (*g_actionDeps->fileRecvTarget).clear();
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvFile) = nullptr;   // 句柄前面已经关掉了
            IslandOverlay::instance()->clearFileTransfer();   // #198：字节不符 ⇒ 进度卡撤掉
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget)));
        out.data.insert(QStringLiteral("bytes"), (*g_actionDeps->fileRecvBytes));
        out.data.insert(QStringLiteral("chunks"), (*g_actionDeps->fileRecvNext));
        // ── 2026-10-11：「收到文件」的提示从这里发，不再在 file_push 里发 ──
        // 以前开会话的瞬间就报"收到文件"，可那时文件刚被 Truncate 成 0 字节、
        // 一片数据都还没到 —— 学生/老师点开就是个空文件（用户报的"推完的文件是空的"）。
        // 现在放到字节数**比对通过之后**，文件名带体积，让人一眼知道收全了没有。
        {
            const QString shown = QDir::toNativeSeparators((*g_actionDeps->fileRecvTarget));
            const QString desc = QFileInfo((*g_actionDeps->fileRecvTarget)).fileName()
                                 + QStringLiteral("（") + g_actionDeps->humanBytes((*g_actionDeps->fileRecvBytes))
                                 + QStringLiteral("）");
            islandShow(QStringLiteral("收到文件"), desc, QStringLiteral("download"), 6000,
                       true, shown);
            trayNotify(QStringLiteral("收到文件"), desc, QSystemTrayIcon::Information, 10000);
            IslandOverlay::instance()->clearFileTransfer();   // #198：收全 ⇒ 进度卡撤掉，改显"收到文件"
        }
        qInfo("[agent-qt] ✅ 文件收全落盘：%s（%lld 字节 / %d 片）",
              qPrintable((*g_actionDeps->fileRecvTarget)), (long long)(*g_actionDeps->fileRecvBytes), (*g_actionDeps->fileRecvNext));
        (*g_actionDeps->fileRecvTarget).clear();
        (*g_actionDeps->fileRecvNext) = 0;
        (*g_actionDeps->fileRecvBytes) = 0;
        (*g_actionDeps->fileRecvLastMs) = 0;
        return out;
    }

    // ── file_push：接收远程发来的文件 ──
    // 走独立的消息类型（file_chunk / file_done），见下方 handleControlText 的分支。
    // 这个 action 只是"初始化接收会话"：校验目标路径、清掉旧的半截文件。
    if (action == QStringLiteral("file_push")) {
        handled = true;
        const QString fname = params.value(QStringLiteral("name")).toString().trimmed();
        const qint64 total = params.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
        if (fname.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 需要 name（文件名）");
            return out;
        }
        if (!fname.contains(QLatin1Char('.'))) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名 %1 缺少扩展名，拒绝（防无后缀可执行）").arg(fname);
            return out;
        }
        // 目标必须落在收件目录内：只取文件名，丢弃任何路径成分
        const QString clean = QFileInfo(fname).fileName();
        if (clean != fname) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("file_push: 文件名不能带路径（收到 %1）").arg(fname);
            return out;
        }
        // #198（2026-10-11）：进度卡用。先把名字/总字节记下来，开好句柄后在成功分支点亮灵动岛。
        (*g_actionDeps->fileRecvName) = clean;
        (*g_actionDeps->fileRecvTotal) = total;
        QDir().mkpath((*g_actionDeps->recvDir));
        const QString target = (*g_actionDeps->recvDir) + QStringLiteral("/") + clean;
        QFile::remove(target);   // 重名直接覆盖旧文件

        // 开接收句柄（2026-10-04 补上）。原来这两样（收件目录 + 这个句柄）都只有声明没有实现：
        // 结果就是落盘路径跑到当前盘根目录、file_chunk 一律"接收文件句柄已关闭"。
        // 顺带先摘掉上一次没收口的会话 —— 不摘的话两个会话抢同一个句柄，写进去的两批数据会搅一起。
        if ((*g_actionDeps->fileRecvFile)) {
            (*g_actionDeps->fileRecvFile)->close();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
        }
        (*g_actionDeps->fileRecvTarget) = target;
        (*g_actionDeps->fileRecvBytes) = 0;
        (*g_actionDeps->fileRecvNext) = 0;
        (*g_actionDeps->fileRecvFile) = new QFile(target);
        // Truncate 显式带上：只给 WriteOnly 时，撞上"文件存在但处于删除挂起状态"
        // （刚被 QFile::remove 掉、而句柄还没被系统真正释放）会直接失败。
        if (!(*g_actionDeps->fileRecvFile)->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // 失败必须带**系统的原话**回来。以前只报"目标打不开（路径）"，
            // 于是目录不存在 / 没权限 / 被别的进程占着 / 删不掉 —— 四种病长得一模一样，
            // 每次都得从零猜一遍（2026-10-07 就因为这句含糊的话误判成"目录没建"）。
            const QString why = (*g_actionDeps->fileRecvFile)->errorString();
            delete (*g_actionDeps->fileRecvFile);
            (*g_actionDeps->fileRecvFile) = nullptr;
            // 同名打不开就换个名字再试一次：收件目录里撞名的往往是上一次没收口的残留，
            // 换名能让这次推送**成功**，而不是让用户对着一句"打不开"干瞪眼。
            const QString alt = (*g_actionDeps->recvDir) + QStringLiteral("/") +
                                QFileInfo(clean).completeBaseName() + QStringLiteral("-") +
                                QString::number(QDateTime::currentSecsSinceEpoch()) +
                                QStringLiteral(".") + QFileInfo(clean).suffix();
            (*g_actionDeps->fileRecvFile) = new QFile(alt);
            if (!(*g_actionDeps->fileRecvFile)->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("file_push: 目标打不开（%1）：%2；换名 %3 也失败：%4")
                                .arg(target, why, alt, (*g_actionDeps->fileRecvFile)->errorString());
                delete (*g_actionDeps->fileRecvFile);
                (*g_actionDeps->fileRecvFile) = nullptr;
                (*g_actionDeps->fileRecvTarget).clear();
                return out;
            }
            qInfo("[agent] file_push 原名打不开（%s），已换名落盘 %s",
                  qPrintable(why), qPrintable(alt));
            (*g_actionDeps->fileRecvTarget) = alt;
            (*g_actionDeps->fileRecvBytes) = 0;
            (*g_actionDeps->fileRecvNext) = 0;
            (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(alt));
            out.data.insert(QStringLiteral("totalBytes"), total);
            out.data.insert(QStringLiteral("ready"), true);
            IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), 0, (*g_actionDeps->fileRecvTotal));
            // ⚠️ 这里**不提示**"收到文件"。此刻只是把接收句柄开好、目标刚被 Truncate 成
            // 0 字节，一片数据都还没到 —— 在这儿报"收到"就是让人去打开一个空文件
            // （2026-10-11 用户报的"推完的文件是空的"正是这条）。
            // 真正的提示挪到 file_done 字节数比对通过之后。
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), QDir::toNativeSeparators(target));
        out.data.insert(QStringLiteral("totalBytes"), total);
        out.data.insert(QStringLiteral("ready"), true);
        IslandOverlay::instance()->setFileTransfer((*g_actionDeps->fileRecvName), 0, (*g_actionDeps->fileRecvTotal));
        (*g_actionDeps->fileRecvLastMs) = QDateTime::currentMSecsSinceEpoch();   // 同上：此刻不提示
        return out;
    }

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
