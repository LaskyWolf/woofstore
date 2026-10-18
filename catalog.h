#pragma once
#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

enum class Source { Flatpak, Repo, Aur };

// One installable thing from one source.
struct Package {
    Source source = Source::Repo;
    QString name;     // Flatpak app ID, repo package name, or AUR package name
    QString repo;     // flathub, extra, cachyos-v3, aur
    QString version;
    double popularity = 0;  // Source-native score; compare only within one source.
};

struct Link {  // AppStream URL kind ("homepage", "vcs-browser", ...) and address
    QString type, url;
    bool operator==(const Link &other) const { return type == other.type && url == other.url; }
};

enum class MetadataOrigin { None, AppStream, Overlay };

// One entry on screen. Packages lists every source that provides it.
struct CatalogApp {
    QString key;  // Normalized AppStream ID, or "pkg:<name>" / "aur:<name>" without metadata
    QString name, summary, description, homepage, iconPath;
    QStringList categories;
    QList<QUrl> screenshots;
    QList<Link> links;  // Homepage first when present
    QStringList keywords;
    QList<Package> packages;
    QString componentId;  // Original AppStream ID, for add-on lookups
    QString desktopId;    // Launcher file name, to open an installed app
    QString releaseVersion, releaseNotes;
    QDate releaseDate;
    struct Release { QString version; QDate date; QString notes; };
    QList<Release> olderReleases;  // After the latest, newest first; notes are markup
    MetadataOrigin origin = MetadataOrigin::None;
};

// Popularity signals from Flathub and pkgstats; empty when never downloaded.
struct Rankings {
    QStringList popular, trending;          // Flathub app IDs, best first
    QStringList added, updated;             // Flathub app IDs, newest first
    QHash<QString, double> flathubInstalls; // Flathub app ID -> installs last month
    QHash<QString, double> archPopularity;  // pkgname -> % of pkgstats submitters
    bool offline = false;                   // Shown from the saved copy
};

struct RepoPackage { QString name, repo, version, description, url; };

// Packages with zero votes keep only name and version: exact-name search still finds them.
struct AurEntry {
    QString name, version, description, url;
    int votes = 0;
    double popularity = 0;
    bool detailed() const { return votes > 0; }
};

QString normalizeComponentId(QString id);
bool isLibraryName(const QString &name);
// expac lines "repo\tname\tversion\tdescription[\turl]"; the first repo in pacman.conf order wins, as in pacman.
QList<RepoPackage> parseSyncPackages(const QString &expacOutput, const QStringList &repoOrder);
QByteArray gunzip(const QByteArray &data, bool *ok = nullptr);
QList<AurEntry> parseAurDump(const QByteArray &json, bool *ok = nullptr);
bool writeAurCache(const QString &path, const QList<AurEntry> &entries);
QList<AurEntry> readAurCache(const QString &path, bool *ok = nullptr);
// Flathub /collection/popular and /collection/trending, pkgstats /api/packages. Bad input yields empty parts.
Rankings parseRankings(const QByteArray &popular, const QByteArray &trending, const QByteArray &pkgstats, const QByteArray &added = {}, const QByteArray &updated = {});
// One comparable popularity score per entry, about 0..1, from each source's own signal.
double appScore(const CatalogApp &app, const Rankings &rankings);
// The Explore category group for AppStream categories, or empty.
QString categoryGroup(const QStringList &categories);
// Indexes of entries matching query, best first: exact name, name prefix, name or package, then a word in keywords or summary.
QList<int> rankSearch(const QList<CatalogApp> &apps, const QString &query, const QList<double> &scores);
