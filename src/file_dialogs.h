#pragma once
#include <QObject>
#include <QString>

// QFileDialog 包装：QML 里没法直接 new QFileDialog（Widgets 控件），
// 通过这个 QObject 的 Q_INVOKABLE 弹原生对话框取路径。
// 纯同步调用（QFileDialog::getSaveFileName/getOpenFileName 阻塞到用户选完），
// 返回值：选中路径；取消返回空串。
class FileDialogs : public QObject {
    Q_OBJECT
public:
    explicit FileDialogs(QObject* parent = nullptr);

    // 保存文件对话框（filter 如 "JSON (*.json)"）；空 = 取消
    Q_INVOKABLE QString getSavePath(const QString& title, const QString& defaultName,
                                    const QString& filter) const;
    // 打开文件对话框；空 = 取消
    Q_INVOKABLE QString getOpenPath(const QString& title, const QString& filter) const;
};
