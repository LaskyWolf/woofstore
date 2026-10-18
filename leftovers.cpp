#include "leftovers.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QSet>

static qint64 sizeOf(const QString &path) {
    const QFileInfo info(path);
    if (info.isSymLink() || info.isFile()) return info.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);  // Symlinks to folders are not followed
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

QList<Leftover> findLeftovers(const QString &home, const QStringList &names) {
    // Folders every desktop shares: a package called "icons" must never select them.
    static const QSet<QString> shared{"applications", "icons", "themes", "fonts", "share", "bin", "systemd", "autostart", "dconf", "pulse", "mime", "locale", "kde", "gtk-3.0", "gtk-4.0", "trash", "flatpak", "recently-used.xbel"};
    QSet<QString> wanted;
    for (const QString &name : names) {
        const QString lower = name.toLower();
        if (lower.size() < 3 || shared.contains(lower)) continue;
        wanted.insert(lower); wanted.insert(lower + "rc");
    }
    const QList<QPair<QString, QString>> places{{".config", "Settings"}, {".local/share", "Data"}, {".local/state", "State"}, {".cache", "Cache"}};
    QList<Leftover> found;
    for (const auto &place : places) {
        const QDir dir(home + '/' + place.first);
        for (const QFileInfo &entry : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (!wanted.contains(entry.fileName().toLower())) continue;
            found.append({entry.absoluteFilePath(), place.second, sizeOf(entry.absoluteFilePath()), place.second == "Cache"});
        }
    }
    return found;
}

QList<Leftover> flatpakData(const QString &home, const QString &appId) {
    const QString path = home + "/.var/app/" + appId;
    if (appId.isEmpty() || appId.contains('/') || !QFileInfo(path).isDir()) return {};
    return {{path, "Data", sizeOf(path), false}};
}

bool deleteLeftover(const QString &home, const QString &path) {
    const QFileInfo info(path);
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();  // The folder, so a symlinked entry itself can go
    const QString root = QFileInfo(home).canonicalFilePath();
    if (root.isEmpty() || parent.isEmpty() || !(parent + QLatin1Char(0x2f)).startsWith(root + QLatin1Char(0x2f)) || (!info.exists() && !info.isSymLink())) return false;
    if (info.isSymLink() || info.isFile()) return QFile::remove(path);
    return QDir(path).removeRecursively();
}

QString formatSize(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
