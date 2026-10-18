#pragma once
#include <QCoreApplication>
#include <QString>

// A single catalog context covers both UI and worker-produced user messages.
inline QString dlTr(const char *source)
{
    return QCoreApplication::translate("DiskLens", source);
}
