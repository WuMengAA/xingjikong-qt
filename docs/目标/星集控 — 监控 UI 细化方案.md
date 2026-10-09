# 星集控 — 监控 UI 细化方案

> 分层监控：多班级面板只看低频快照（省带宽），单班监控才开双路实时流（信息完整）。

---

## 一、整体分层策略

| 层级 | 画面 | 刷新方式 | 带宽 | 用途 |
|---|---|---|---|---|
| **多班级面板** | 屏幕缩略图 | 15 秒截图 | 极低 | 巡课、状态总览 |
| **单班监控** | 屏幕 + 摄像头双画面 | WebRTC 实时 | 中 | 深入查看 |
| **展板** | 屏幕缩略图 | 15 秒截图 | 极低 | 公共展示 |

**关键决策**：
- 多班级面板**不显示摄像头**，节省带宽也保护隐私
- 摄像头只在单班监控时开启，**按需拉流**
- 屏幕缩略图 15 秒一次，比之前设计的 5-10 秒更保守，进一步降带宽

---

## 二、多班级面板（15 秒截图）

### 2.1 被控端截图逻辑

被控端定时（15 秒）截屏，压缩成 JPEG，上传到后端。分辨率降到 **320×180**，单张约 15-25KB。

```cpp
// ScreenSnapshotTimer.cpp
class ScreenSnapshotTimer : public QObject {
    Q_OBJECT
public:
    void start() {
        m_timer.setInterval(15000);  // 15 秒
        connect(&m_timer, &QTimer::timeout, this, &ScreenSnapshotTimer::capture);
        m_timer.start();
    }

private:
    void capture() {
        QScreen *screen = QGuiApplication::primaryScreen();
        QPixmap pixmap = screen->grabWindow(0);

        // 缩放到 320x180
        QPixmap scaled = pixmap.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // 压缩为 JPEG（质量 60，体积小）
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "JPEG", 60);

        // 上传
        emit snapshotReady(jpeg);
    }

    QTimer m_timer;
};
```

**优化点**：
- 如果画面和上一帧差异 < 1%，跳过上传（帧差检测）
- 上传失败不重试，等下一轮
- 只在"多班级面板被订阅"时才截图（按需截图）

### 2.2 后端缓存

后端为每台设备缓存最新一张缩略图：

```js
// snapshotCache.js
const cache = new Map();  // deviceId → { data, ts }

export function updateSnapshot(deviceId, data) {
  cache.set(deviceId, { data, ts: Date.now() });
}

export function getSnapshot(deviceId) {
  return cache.get(deviceId);
}

// 推送订阅者
function broadcast(deviceId, snapshot) {
  boards.forEach(ws => {
    ws.send(JSON.stringify({
      type: 'device.thumbnail',
      payload: { deviceId, data: snapshot.data, ts: snapshot.ts }
    }));
  });
}
```

### 2.3 带宽估算

| 参数 | 值 |
|---|---|
| 单张 JPEG 大小 | 15-25KB |
| 截图频率 | 15 秒一次 |
| 单台带宽 | ~1.3 KB/s |
| 60 台合计 | ~78 KB/s = 624 kbps |
| 服务器下行（给展板） | 60 路 × 1.3 KB/s = 78 KB/s |

**结论**：带宽极低，完全可承受。即使 200 台设备也才 2Mbps。

### 2.4 前端展示

```qml
// DeviceCard.qml
Rectangle {
    property var device

    ColumnLayout {
        // 状态色条
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 3
            color: statusColor(device.status)
        }

        // 缩略图
        Image {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: device.thumbnail ? "data:image/jpeg;base64," + device.thumbnail : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        // 班级名 + 状态文字
        RowLayout {
            Text { text: device.name }
            Text { text: statusText(device.status); color: statusColor(device.status) }
        }
    }
}
```

---

## 三、单班监控（屏幕 + 摄像头双画面）

### 3.1 布局方案

**左右分屏**（推荐）：屏幕画面占 60%，摄像头占 40%。

```
┌─────────────────────────────────────────────────┐
│ 高一1班                          [返回] [全屏]   │
├─────────────────────────────┬───────────────────┤
│                             │                   │
│                             │                   │
│                             │                   │
│        屏幕画面              │    摄像头画面      │
│        (WebRTC 720p)        │    (WebRTC 480p)  │
│                             │                   │
│                             │                   │
│                             │                   │
├─────────────────────────────┴───────────────────┤
│ 状态：● 在线   IP：10.0.1.23   最后心跳：2s 前   │
│ [锁屏] [关机] [重启] [消息] [文件] [进程]        │
└─────────────────────────────────────────────────┘
```

**为什么左右分屏**：
- 屏幕是 16:9，摄像头常见也是 16:9 或 4:3，左右分屏利用率高
- 老师主要看屏幕，摄像头是辅助，60/40 比例合理
- 上下分屏会导致两个画面都太扁

**可切换**：提供"仅屏幕""仅摄像头""双画面"三种模式，老师按需切换。

### 3.2 摄像头采集

被控端在收到"开启摄像头"指令后，才启动摄像头采集：

```cpp
// CameraStreamer.cpp
class CameraStreamer : public QObject {
    Q_OBJECT
public:
    void start() {
        // 用 Qt Multimedia 采集
        QCamera *camera = new QCamera(QMediaDevices::defaultVideoInput());
        QMediaCaptureSession session;
        session.setCamera(camera);

        // 输出到 QVideoSink
        QVideoSink *sink = new QVideoSink();
        session.setVideoOutput(sink);

        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            // 转成 QImage，编码为 H.264，推给 WebRTC
            emit frameReady(frame);
        });

        camera->start();
    }

    void stop() {
        // 关闭摄像头，释放资源
    }
};
```

**参数**：
- 分辨率：640×480（够看，不占带宽）
- 帧率：15fps
- 码率：200kbps

### 3.3 双路 WebRTC

被控端同时推两路流：

```cpp
// 屏幕流
peerConnection.addTrack(screenTrack, screenStream);

// 摄像头流
peerConnection.addTrack(cameraTrack, cameraStream);
```

管理端同时拉两路：

```qml
RowLayout {
    // 屏幕
    VideoOutput {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 3
        source: screenStream
    }

    // 摄像头
    VideoOutput {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 2
        source: cameraStream
    }
}
```

### 3.4 带宽估算（单班）

| 流 | 分辨率 | 帧率 | 码率 |
|---|---|---|---|
| 屏幕 | 1280×720 | 30fps | 500kbps |
| 摄像头 | 640×480 | 15fps | 200kbps |
| **合计** | | | **700kbps** |

**全局限制 1-2 路**，老师同时只看一个班，带宽可承受。

### 3.5 摄像头不可用处理

如果被控端没有摄像头或摄像头被占用：

```cpp
void CameraStreamer::start() {
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        emit unavailable("未检测到摄像头");
        return;
    }
    // ...
}
```

前端显示占位图 + "摄像头不可用"。

---

## 四、QML 实现骨架

### 4.1 单班监控视图

```qml
// ClassMonitorView.qml
Item {
    id: root
    property string deviceId
    property int layoutMode: 0  // 0=双画面, 1=仅屏幕, 2=仅摄像头

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部栏
        TopBar {
            Layout.fillWidth: true
            title: deviceModel.get(root.deviceId).name
            onBack: stackView.pop()
        }

        // 画面区
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            // 屏幕画面
            VideoOutput {
                visible: root.layoutMode !== 2
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.layoutMode === 0 ? 3 : 1
                source: screenStream
            }

            // 摄像头画面
            VideoOutput {
                visible: root.layoutMode !== 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.layoutMode === 0 ? 2 : 1
                source: cameraStream
            }
        }

        // 底部信息 + 操作
        BottomBar {
            Layout.fillWidth: true
            device: deviceModel.get(root.deviceId)
            layoutMode: root.layoutMode
            onLayoutModeChanged: root.layoutMode = layoutMode
        }
    }
}
```

### 4.2 布局切换

```qml
// BottomBar.qml
RowLayout {
    ButtonGroup { id: layoutGroup }

    Button {
        text: "双画面"
        checkable: true
        checked: parent.layoutMode === 0
        ButtonGroup.group: layoutGroup
        onClicked: parent.layoutMode = 0
    }
    Button {
        text: "仅屏幕"
        checkable: true
        checked: parent.layoutMode === 1
        ButtonGroup.group: layoutGroup
        onClicked: parent.layoutMode = 1
    }
    Button {
        text: "仅摄像头"
        checkable: true
        checked: parent.layoutMode === 2
        ButtonGroup.group: layoutGroup
        onClicked: parent.layoutMode = 2
    }
}
```

---

## 五、与现有设计的关系

| 现有设计 | 本次调整 |
|---|---|
| 多班级缩略图 5-10 秒刷新 | 改为 **15 秒**，更省带宽 |
| 单班监控只有屏幕 | 增加**摄像头双画面** |
| 摄像头不在面板显示 | 确认，只在单班监控开启 |
| 全局限制 1-2 路实时流 | 保持，但每路变成"屏幕+摄像头"两流 |
| 缩略图 320×180 | 保持 |

---

## 六、隐私与安全

| 项 | 措施 |
|---|---|
| 摄像头按需开启 | 只有单班监控被打开时才采集，关闭立即停止 |
| 学生知情 | 被控端灵动岛显示"摄像头已开启" |
| 审计 | 每次开启摄像头记录：谁、什么时候、哪个班、开了多久 |
| 学校可配置 | 可全局禁用摄像头功能 |
| 录制 | 摄像头流默认不录制，只有考试模式等特定场景才录制 |

**关键**：摄像头是敏感功能，默认**按需开启 + 学生知情 + 全程审计**。

---

## 七、开发顺序

| 阶段 | 任务 |
|---|---|
| 第 1 周 | 多班级面板 15 秒截图（屏幕） |
| 第 2 周 | 单班监控屏幕 WebRTC |
| 第 3 周 | 单班监控摄像头 WebRTC |
| 第 4 周 | 双画面布局 + 切换 |
| 第 5 周 | 隐私保护 + 审计 |

**第一版只做**：多班级 15 秒截图 + 单班屏幕 WebRTC。摄像头第二版再加。

---

**一句话总结**：多班级面板用 15 秒低频截图（只显示屏幕，省带宽），单班监控用双路 WebRTC（屏幕 + 摄像头，左右分屏）。摄像头按需开启、学生知情、全程审计。这个分层策略既省带宽又保护隐私，是教学场景的最优解。
