#include "file_dialogs.h"
#include <QFileDialog>
#include <QCoreApplication>

FileDialogs::FileDialogs(QObject* parent)
    : QObject(parent)
{
}

QString FileDialogs::getSavePath(const QString& title, const QString& defaultName,
                                 const QString& filter) const
{
    return QFileDialog::getSaveFileName(nullptr, title, defaultName, filter);
}

QString FileDialogs::getOpenPath(const QString& title, const QString& filter) const
{
    return QFileDialog::getOpenFileName(nullptr, title, QString(), filter);
}
