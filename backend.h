#pragma once
#include <QObject>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <AppStreamQt/pool.h>
#include "catalog.h"
#include "merge.h"
#include <functional>
#include <memory>

struct App {
    QString id, name, summary, version, source, remote, scope, branch, base, path, homepage;
    QIcon icon;
    QUrl screenshot;
};
using Apps = QList<App>;

QString plainDescription(const QString &html);
QString desktopQuote(const QString &path);
bool validPackageName(const QString &name);
QString importAppImage(const QString &file, const QString &dataRoot, QString *error);
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
    QStringList userRemotes, systemRemotes;
    void load();
    void loadInstalled();
    void loadRankings();
    // Test and preview data in place of the live sources: {apps, rankings, aur}. Emits catalogReady and rankingsReady.
    bool loadFixture(const QString &path);
    // Downloads once through the disk cache; local files load directly. done runs only while context lives.
    void fetchImage(const QUrl &url, QObject *context, std::function<void(QPixmap)> done);
    QList<CatalogApp> addons(const CatalogApp &app) const;
    void searchAur(const QString &query, std::function<void(Apps, QString)> done);
    void prepareAur(const App &app, std::function<void(QString, QString, QString)> done);
    bool reviewedFilesUnchanged(QString *error) const;
    QString reviewPath() const;
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
    void loadRepos();
    void loadAur();
    void sourceDone();
    void assemble();
};
