#pragma once
#include <QObject>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <AppStreamQt/pool.h>
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

class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject *parent = nullptr);
    Apps catalogue, installed;
    QStringList userRemotes, systemRemotes;
    void load();
    void loadInstalled();
    void searchAur(const QString &query, std::function<void(Apps, QString)> done);
    void prepareAur(const App &app, std::function<void(QString, QString, QString)> done);
    bool reviewedFilesUnchanged(QString *error) const;
    QString reviewPath() const;
    void readCommand(const QString &program, const QStringList &args,
                     std::function<void(int, QString, QString)> done, int timeout = 30000);
signals:
    void catalogueChanged();
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
};
