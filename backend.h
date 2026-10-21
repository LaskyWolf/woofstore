#pragma once
#include <QObject>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <AppStreamQt/pool.h>
#include "catalog.h"
#include "merge.h"
#include "review.h"
#include "ratings.h"
#include <functional>
#include <memory>
#include <optional>

struct App {
    QString id, name, summary, version, source, remote, scope, branch, base, path, homepage, launcher;
    QIcon icon;
    QUrl screenshot;
};
using Apps = QList<App>;

// A menu entry (.desktop file) the user can start.
struct Launcher { QString id, name, comment, icon, exec, path; bool flatpak = false; };
// Empty when the entry is hidden, is not an application, is for another desktop, or only starts a Steam game.
std::optional<Launcher> parseLauncher(const QString &path, const QByteArray &text, const QString &language);
QList<Launcher> scanLaunchers(const QStringList &dirs, const QString &language);

// Both versions as rich text with only the part that changed coloured: the old one red, the new one green.
QPair<QString, QString> versionDiff(const QString &from, const QString &to);

struct UpdateEntry { QString id, name, from, to, source, url; };  // source: Flatpak, Arch, AUR or AppImage; url: where an AppImage update downloads from
QList<UpdateEntry> parseArchUpdates(const QString &text, const QString &source);
QList<UpdateEntry> parseFlatpakUpdates(const QString &text, const QHash<QString, QString> &installedVersions);

// Where an AppImage says its newer versions are, read from the ".upd_info" section the publisher embedded. Empty when it has none.
QString appImageUpdateInfo(const QString &file);
struct UpdateInfo { QString zsyncUrl, owner, repo, tag, pattern; };  // zsyncUrl is set for "zsync|url"; owner, repo, tag and pattern for "gh-releases-zsync|..."
std::optional<UpdateInfo> parseUpdateInfo(const QString &info);
// The "SHA-1:" line of a .zsync file is the checksum of the newest file; equal to the installed file's checksum means up to date.
QString zsyncSha1(const QByteArray &header);
// The release asset matching the publisher's pattern, with the address of its .zsync file; empty when there is none.
struct ReleaseAsset { QString url, zsyncUrl, version; };
ReleaseAsset pickReleaseAsset(const QByteArray &releaseJson, const QString &pattern);

// A news item from a feed or a blog. important: from the distribution itself, so it counts for the sidebar badge.
struct NewsItem { QString source, title, summary, url; QDateTime date; bool important = false; QString image; };  // image: address of the cover picture, empty when the feed has none
QList<NewsItem> parseFeed(const QByteArray &xml, const QString &source, bool important);
QList<NewsItem> parseCachyBlog(const QString &html);

QString plainDescription(const QString &html);
QString desktopQuote(const QString &path);
bool validPackageName(const QString &name);
struct Orphan { QString name, size; };
QList<Orphan> parseOrphans(const QString &text);  // Lines of "name" or "name<TAB>size"; names pacman would not accept are dropped
QString importAppImage(const QString &file, const QString &dataRoot, QString *error);
// The bash script of an update run: $1 is this program, the other arguments are "id<TAB>url<TAB>name" per AppImage. Each step prints "==> <name>" first.
// A failed system upgrade stops everything; a failed AUR, Flatpak or AppImage step is reported and the script goes on, ending with status 1.
QString updateScript(bool system, bool aur, bool devel, bool flatpak, bool appImages, QStringList *stepNames);
// The pacman.conf for the AUR build chroot, made from the host's: same repositories and signature levels (so multilib and CachyOS
// resolve), host-only paths dropped, and every "Include" replaced by that file's Server lines (the chroot has no mirrorlist files of its own).
// readFile returns the text of a file, or an empty string when it cannot be read. Repositories left without a server are dropped.
QString chrootPacmanConf(const QString &hostConf, const std::function<QString(const QString &)> &readFile);
// The bash script of an AUR build: $1 package name, $2 "--needed" or "", $3 chroot directory holding pacman.conf (see chrootPacmanConf),
// then one "name<TAB>folder" per package to build, dependencies first and $1 last. Creates the chroot the first time, otherwise updates it,
// builds each folder (each build sees the packages built before it) and installs the exact built files: dependencies as dependencies, then $1.
QString aurBuildScript();
// Dependencies listed in a .SRCINFO (depends, makedepends, checkdepends), with version constraints, without the package's own names.
QStringList srcinfoDependencies(const QString &srcinfo);
QString dependencyName(const QString &dependency);  // "lito>=0.8.5" -> "lito"
// The order to build root and the AUR packages it needs: every package after the ones it needs. needs maps a package to its AUR dependencies.
QStringList aurBuildOrder(const QString &root, const QMap<QString, QStringList> &needs);
// The build files of one package as the user reviewed them; the folder stays alive as long as the snapshot does.
struct ReviewSnapshot {
    std::shared_ptr<QTemporaryDir> dir;
    QMap<QString, QByteArray> hashes;
    QString path() const { return dir ? dir->path() + "/package" : QString(); }
};
bool reviewUnchanged(const ReviewSnapshot &snapshot, QString *error);  // False when a reviewed file changed, appeared or went missing since the review
// Imports a newer file of an imported AppImage and removes the old copy and its launcher; the old one stays when the import fails.
QString replaceAppImage(const QString &file, const QString &oldId, const QString &dataRoot, QString *error);
Apps importedAppImages(const QString &dataRoot);
QStringList metadataDirs();

class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject *parent = nullptr);
    Apps catalogue, installed;
    QList<CatalogApp> apps;  // One entry per app across every source; filled before catalogReady()
    QList<AurEntry> aur;     // Every AUR package; zero-vote packages carry no details
    bool osMetadataMissing = false;  // archlinux-appstream-data is not installed
    MergeStats mergeStats;
    Rankings rankings;
    bool fixtureMode = false;  // Checks use a fixed catalogue and must not call the network for ratings
    QStringList userRemotes, systemRemotes;
    void load();
    void loadInstalled();
    void loadRankings();
    // Ratings for the details page; done runs once with whatever the sources returned (offline: the last saved answers).
    void ratings(const CatalogApp &app, std::function<void(Ratings)> done);
    // GET url, saved for a day in the cache folder; when offline the last saved copy is used. Empty when neither exists.
    void cachedGet(const QUrl &url, std::function<void(QByteArray)> done);
    void aurInfo(const QString &name, std::function<void(AurInfo, QString)> done);
    bool installedPackage(const Package &package) const;
    // Pending updates of Flatpak, repository and AUR packages; the list holds what was found, errors what could not be checked.
    // Forget the saved web answers and download the rankings and the installed list again.
    void refresh();
    void checkUpdates(std::function<void(QList<UpdateEntry>, QStringList)> done);
    // Newest first, from the Arch, CachyOS, kernel, KDE, LWN, 9to5Linux, Phoronix, GamingOnLinux, OMG! Ubuntu, Fedora Magazine, Linuxiac and It's FOSS feeds; problems lists the sources that did not answer.
    void loadNews(std::function<void(QList<NewsItem>, QStringList)> done);
    QSet<QString> nativePackages;  // Installed packages from the sync repos (pacman -Qqn); the Installed page lists only foreign ones
    // Test and preview data in place of the live sources: {apps, rankings, aur}. Emits catalogReady and rankingsReady.
    bool loadFixture(const QString &path);
    // Downloads once through the disk cache; local files load directly. done runs only while context lives.
    void fetchImage(const QUrl &url, QObject *context, std::function<void(QPixmap)> done);
    QList<CatalogApp> addons(const CatalogApp &app) const;
    void searchAur(const QString &query, std::function<void(Apps, QString)> done);
    void prepareAur(const App &app, std::function<void(QString, QString, QString)> done);
    bool reviewedFilesUnchanged(QString *error) const;
    QString reviewPath() const;
    ReviewSnapshot reviewSnapshot() const { return {review, reviewedHashes}; }
    // Calls done with the dependencies (as written, with versions) that no configured repository can provide.
    void missingFromRepos(const QStringList &dependencies, std::function<void(QStringList)> done);
    void orphans(std::function<void(QList<Orphan>)> done);  // Packages installed as dependencies that nothing needs any more
    void readCommand(const QString &program, const QStringList &args,
                     std::function<void(int, QString, QString)> done, int timeout = 30000);
signals:
    void catalogueChanged();
    void catalogReady();
    void rankingsReady();
    void installedChanged();
    void issue(const QString &message);
private:
    AppStream::Pool pool;
    QNetworkAccessManager network;
    std::shared_ptr<QTemporaryDir> review;
    QMap<QString, QByteArray> reviewedHashes;
    bool reviewComplete = false;
    QMap<QString, QString> flatpakOrigins, flatpakVersions;
    quint64 reviewGeneration = 0;
    quint64 installedGeneration = 0;
    QList<RepoPackage> repoPackages;
    MergeOverrides overrides;
    QHash<QString, QList<CatalogApp>> fixtureAddons;  // Add-ons by app key, from loadFixture
    int pendingSources = 0;
    void checkAppImages(std::function<void(QList<UpdateEntry>, QStringList)> done);
    void fetch(const QUrl &url, std::function<void(QByteArray, QString)> done, qint64 firstBytes = 0);
    void loadRepos();
    void loadAur();
    void sourceDone();
    void assemble();
};
