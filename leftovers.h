#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Files an uninstalled app commonly leaves in the user's home folder.
struct Leftover {
    QString path, kind;  // kind: "Cache", "Settings", "Data", "State"
    qint64 bytes = 0;
    bool checkedByDefault = false;  // Only caches: they are rebuilt on demand; settings and data may hold the user's work
};

// Entries of ~/.config, ~/.local/share, ~/.local/state and ~/.cache whose names equal one of names
// (case-insensitive, "<name>rc" too). Exact matches only; names shorter than three characters are ignored.
QList<Leftover> findLeftovers(const QString &home, const QStringList &names);
// ~/.var/app/<id>: where Flatpak keeps an app's data. Empty when absent.
QList<Leftover> flatpakData(const QString &home, const QString &appId);
// Removes a path inside home; a symlink is removed without touching its target. Refuses anything outside home.
bool deleteLeftover(const QString &home, const QString &path);
QString formatSize(qint64 bytes);
