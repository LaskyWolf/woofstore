#include "backend.h"
#include <AppStreamQt/bundle.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/image.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFutureWatcher>
#include <QLocale>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPixmapCache>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <malloc.h>

QString plainDescription(const QString &html) {
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

bool validPackageName(const QString &name) {
    static const QRegularExpression pattern("^[a-zA-Z0-9][a-zA-Z0-9@._+\\-]*$");
    return name.size() <= 200 && pattern.match(name).hasMatch();
}

QString desktopQuote(const QString &path) {
    QString quoted = path;
    // Desktop Exec first unescapes the value, then parses quoted arguments.
    quoted.replace("\\", "\\\\\\\\");
    for (const QChar c : QString("\"`$"))
        quoted.replace(QString(c), QString("\\\\") + c);
    quoted.replace("%", "%%");
    return '"' + quoted + '"';
}

static QByteArray digestFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result();
}

QString importAppImage(const QString &file, const QString &dataRoot, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return QString(); };
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) return fail(source.errorString());
    const QByteArray header = source.read(16);
    if (header.size() < 16 || header.left(4) != QByteArray("\x7f" "ELF", 4)
        || header.mid(8, 2) != "AI" || (header[10] != 1 && header[10] != 2))
        return fail("This file is not a type 1 or type 2 AppImage. Select an AppImage from its publisher.");
    source.seek(0);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&source)) return fail("Could not read the whole AppImage.");
    const QString id = QString::fromLatin1(hash.result().toHex());
    QString title = QFileInfo(file).completeBaseName();
    title.replace(QRegularExpression("[\\x00-\\x1f\\x7f]"), " ");
    title.replace("\\", "_");
    const QString appDir = dataRoot + "/WoofStore/appimages";
    const QString desktopDir = dataRoot + "/applications";
    if (dataRoot.contains('\n') || dataRoot.contains('\r')) return fail("The application data path contains unsupported characters.");
    if (!QDir().mkpath(appDir) || !QDir().mkpath(desktopDir)) return fail("Cannot create the app or launcher folder.");
    const QString dest = appDir + '/' + id + ".AppImage";
    const QString launcher = desktopDir + "/woof-store-" + id + ".desktop";
    if (QFileInfo::exists(dest) || QFileInfo::exists(launcher)) return fail("This AppImage is already imported, or its destination already exists.");
    source.seek(0);
    QSaveFile copy(dest);
    if (!copy.open(QIODevice::WriteOnly)) return fail(copy.errorString());
    QCryptographicHash copyHash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) return fail(source.errorString());
        copyHash.addData(chunk);
        if (copy.write(chunk) != chunk.size()) return fail(copy.errorString());
    }
    if (copyHash.result().toHex() != id.toLatin1()) return fail("The source file changed during import. Try again.");
    if (!copy.commit()) return fail(copy.errorString());
    if (!QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        QFile::remove(dest);
        return fail("Cannot make the imported AppImage executable.");
    }
    QSaveFile desktop(launcher);
    const QByteArray entry = QString("[Desktop Entry]\nType=Application\nName=%1\nExec=%2\nIcon=application-x-executable\nTerminal=false\nCategories=Utility;\nX-WoofStore-AppImage=%3\n")
                                 .arg(title, desktopQuote(dest), id).toUtf8();
    if (!desktop.open(QIODevice::WriteOnly) || desktop.write(entry) != entry.size() || !desktop.commit()) {
        QFile::remove(dest);
        return fail("Cannot save the launcher. The imported copy was removed.");
    }
    if (error) error->clear();
    return dest;
}

Apps importedAppImages(const QString &dataRoot) {
    Apps result;
    const QDir folder(dataRoot + "/WoofStore/appimages");
    for (const QString &file : folder.entryList({"*.AppImage"}, QDir::Files)) {
        App app;
        app.id = QFileInfo(file).completeBaseName();
        app.path = folder.absoluteFilePath(file);
        app.name = app.id;
        QFile launcher(dataRoot + "/applications/woof-store-" + app.id + ".desktop");
        if (launcher.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : launcher.readAll().split('\n'))
                if (line.startsWith("Name=")) app.name = QString::fromUtf8(line.mid(5));
        }
        app.source = "AppImage";
        app.scope = "user";
        app.summary = "Imported portable app";
        app.icon = QIcon::fromTheme("application-x-executable");
        result.append(app);
    }
    return result;
}

// Prefer a cached icon of at least 64 px; theme lookups are deferred to visible cards.
static QString cachedIconPath(const AppStream::Component &component) {
    QString path;
    for (const auto &icon : component.icons()) {
        if (!icon.filename().isEmpty() && QFileInfo::exists(icon.filename())) {
            path = icon.filename();
            if (icon.width() >= 64) break;
        }
    }
    return path;
}

static QList<QUrl> screenshotUrls(const AppStream::Component &component, int limit) {
    QList<QUrl> urls;
    for (const auto &shot : component.screenshotsAll()) {
        const auto image = shot.image(900, 500, 1);
        if (image && image->url().scheme() == "https") urls.append(image->url());
        if (urls.size() == limit) break;
    }
    return urls;
}

// Splits "app/org.kde.krita/x86_64/stable" into ID and branch.
static std::pair<QString, QString> flatpakRef(const AppStream::Bundle &bundle) {
    const QStringList ref = bundle.id().split('/');
    const bool app = ref.value(0) == "app";
    return {app ? ref.value(1) : ref.value(0), app ? ref.value(3) : ref.value(2)};
}

Backend::Backend(QObject *parent) : QObject(parent), pool(this), network(this) {
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images");
    cache->setMaximumCacheSize(200 * 1024 * 1024);
    network.setCache(cache);
    QPixmapCache::setCacheLimit(96 * 1024);  // KiB: a few dozen decoded screenshots
    connect(&pool, &AppStream::Pool::loadFinished, this, [this](bool ok) {
        if (!ok) emit issue("Cannot load the Flatpak catalogue: " + pool.lastError());
        catalogue.clear();
        QSet<QString> seen;
        for (const auto &component : pool.componentsByKind(AppStream::Component::KindDesktopApp)) {
            const auto bundle = component.bundle(AppStream::Bundle::KindFlatpak);
            if (bundle.isEmpty()) continue;
            const auto [id, branch] = flatpakRef(bundle);
            if (id.isEmpty()) continue;
            const QString key = component.origin() + ':' + id + ':' + branch;
            if (seen.contains(key)) continue;
            seen.insert(key);
            App app;
            app.id = id;
            app.name = component.name();
            app.summary = component.summary();
            app.version = "";
            app.source = "Flatpak";
            app.remote = flatpakOrigins.value(id + '/' + branch);
            app.version = flatpakVersions.value(id + '/' + branch);
            app.branch = branch.isEmpty() ? "stable" : branch;
            app.homepage = component.url(AppStream::Component::UrlKindHomepage).toString();
            const QString icon = cachedIconPath(component);
            if (!icon.isEmpty()) app.icon = QIcon(icon);
            app.screenshot = screenshotUrls(component, 1).value(0);
            catalogue.append(app);
        }
        std::sort(catalogue.begin(), catalogue.end(), [](const App &a, const App &b) { return a.name.localeAwareCompare(b.name) < 0; });
        emit catalogueChanged();
        sourceDone();
    });
}

// WoofStore's own metadata (the installed copy, then the source tree during development),
// plus folders listed in WOOFSTORE_EXTRA_METADATA for testing with unpacked catalogues.
QStringList metadataDirs() {
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, "woofstore/metadata", QStandardPaths::LocateDirectory);
    if (QFileInfo(WOOFSTORE_SOURCE_METADATA).isDir()) dirs.append(WOOFSTORE_SOURCE_METADATA);
    dirs += qEnvironmentVariable("WOOFSTORE_EXTRA_METADATA").split(':', Qt::SkipEmptyParts);
    return dirs;
}

void Backend::sourceDone() {
    if (--pendingSources == 0) assemble();
}

void Backend::loadRepos() {
    readCommand("pacman-conf", {"--repo-list"}, [this](int code, QString order, QString error) {
        if (code != 0) { emit issue("Cannot read the pacman repository list: " + error); sourceDone(); return; }
        const QStringList repos = order.split('\n', Qt::SkipEmptyParts);
        readCommand("expac", {"-S", "%r\t%n\t%v\t%d\t%u"}, [this, repos](int code, QString output, QString error) {
            if (code == -1) emit issue("Repository apps need expac. Install it with: sudo pacman -S --needed expac");
            else if (code != 0) emit issue("Cannot list repository packages: " + error);
            else repoPackages = parseSyncPackages(output, repos);
            sourceDone();
        });
    });
}

struct AurResult { QList<AurEntry> entries; QString error; };

// Runs on a worker thread: the dump is parsed at most once a day, every other start reads the compact cache.
static AurResult buildAurIndex(const QString &bin, const QString &gz, const QByteArray &downloaded) {
    bool ok = false;
    if (downloaded.isEmpty()) {
        auto entries = readAurCache(bin, &ok);
        if (ok) return {entries, {}};
    }
    QByteArray compressed = downloaded;
    if (compressed.isEmpty()) {
        QFile file(gz);
        if (file.open(QIODevice::ReadOnly)) compressed = file.readAll();
    }
    if (compressed.isEmpty()) return {{}, "The AUR package list is not downloaded yet. Check your connection and restart WoofStore."};
    const QByteArray json = gunzip(compressed, &ok);
    if (!ok) return {{}, "The downloaded AUR package list is damaged. It will download again on the next start."};
    auto entries = parseAurDump(json, &ok);
    if (!ok) return {{}, "The AUR package list has an unexpected format."};
    if (!writeAurCache(bin, entries)) return {entries, "Cannot save the AUR cache; the list will be parsed again next start."};
    return {entries, {}};
}

void Backend::loadAur() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString gz = dir + "/aur.json.gz", bin = dir + "/aur.bin";
    auto build = [this, bin, gz](const QByteArray &downloaded, const QString &notice) {
        auto *watcher = new QFutureWatcher<AurResult>(this);
        connect(watcher, &QFutureWatcher<AurResult>::finished, this, [this, watcher, notice] {
            const AurResult result = watcher->result();
            watcher->deleteLater();
            aur = result.entries;
            // Parsing the 54 MB JSON leaves freed heap pages the allocator keeps; hand them back to the system.
            malloc_trim(0);
            if (!result.error.isEmpty()) emit issue(result.error);
            else if (!notice.isEmpty()) emit issue(notice);
            sourceDone();
        });
        watcher->setFuture(QtConcurrent::run(buildAurIndex, bin, gz, downloaded));
    };
    const QFileInfo cache(bin);
    if (cache.exists() && cache.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600) { build({}, {}); return; }

    QNetworkRequest request(QUrl("https://aur.archlinux.org/packages-meta-v1.json.gz"));
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);  // Saved as aur.json.gz instead
    const QFileInfo previous(gz);
    if (previous.exists() && cache.exists())
        request.setRawHeader("If-Modified-Since", QLocale::c().toString(previous.lastModified().toUTC(), "ddd, dd MMM yyyy hh:mm:ss 'GMT'").toLatin1());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, bin, gz, build] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            const QByteArray data = reply->readAll();
            QSaveFile file(gz);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit()) {
                // Server time, so the next If-Modified-Since matches the server's clock.
                const QDateTime modified = reply->header(QNetworkRequest::LastModifiedHeader).toDateTime();
                QFile saved(gz);
                if (modified.isValid() && saved.open(QIODevice::ReadWrite)) saved.setFileTime(modified, QFileDevice::FileModificationTime);
            }
            build(data, {});
        } else if (status == 304) {
            QFile cacheFile(bin);  // Unchanged upstream: mark the cache fresh for another day.
            if (cacheFile.open(QIODevice::ReadWrite)) cacheFile.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
            build({}, {});
        } else {
            const QFileInfo old(bin);
            build({}, old.exists() ? "Cannot refresh the AUR package list (" + reply->errorString() + "). Showing the list from "
                                         + QLocale().toString(old.lastModified(), QLocale::ShortFormat) + "." : QString());
        }
    });
}

void Backend::assemble() {
    apps.clear();
    QHash<QString, const RepoPackage *> repoByName;
    for (const auto &package : repoPackages) repoByName.insert(package.name, &package);
    QHash<QString, const AurEntry *> aurByName;
    for (const auto &entry : aur) aurByName.insert(entry.name, &entry);
    QSet<QString> archClaimed, aurClaimed, seenFlatpaks;
    for (const auto &component : pool.componentsByKind(AppStream::Component::KindDesktopApp)) {
        CatalogApp app;
        const bool overlay = component.origin() == "woofstore";
        const auto bundle = component.bundle(AppStream::Bundle::KindFlatpak);
        if (!bundle.isEmpty()) {
            const auto [id, branch] = flatpakRef(bundle);
            // Flathub added as both a system and a user remote lists every app twice.
            if (id.isEmpty() || seenFlatpaks.contains(id + '/' + branch)) continue;
            seenFlatpaks.insert(id + '/' + branch);
            app.packages.append({Source::Flatpak, id, component.origin(), flatpakVersions.value(id + '/' + branch)});
        }
        for (const QString &name : component.packageNames()) {
            if (const RepoPackage *package = repoByName.value(name)) {
                app.packages.append({Source::Repo, package->name, package->repo, package->version});
                if (!overlay) archClaimed.insert(name);
            } else if (const AurEntry *entry = aurByName.value(name)) {  // Our overlay describes AUR packages by name
                app.packages.append({Source::Aur, entry->name, "aur", entry->version, entry->popularity});
                aurClaimed.insert(name);
            }
        }
        if (app.packages.isEmpty()) continue;  // Metadata for a package that no enabled repo provides
        app.key = normalizeComponentId(component.id());
        app.name = component.name();
        app.summary = component.summary();
        app.description = component.description();
        app.homepage = component.url(AppStream::Component::UrlKindHomepage).toString();
        // Arch generates many entries from desktop files without a website; the package URL still identifies the project.
        if (app.homepage.isEmpty())
            for (const auto &package : app.packages) if (package.source == Source::Repo) { app.homepage = repoByName.value(package.name)->url; break; }
        for (const auto kind : {AppStream::Component::UrlKindHomepage, AppStream::Component::UrlKindVcsBrowser, AppStream::Component::UrlKindBugtracker,
                                AppStream::Component::UrlKindHelp, AppStream::Component::UrlKindDonation, AppStream::Component::UrlKindTranslate,
                                AppStream::Component::UrlKindContribute, AppStream::Component::UrlKindFaq}) {
            const QUrl url = component.url(kind);
            if (url.scheme() == "https" || url.scheme() == "http") app.links.append({AppStream::Component::urlKindToString(kind), url.toString()});
        }
        app.categories = component.categories();
        app.keywords = component.keywords();
        app.componentId = component.id();
        if (const auto release = component.releasesPlain().indexSafe(0)) {
            app.releaseVersion = release->version();
            app.releaseDate = release->timestamp().date();
            app.releaseNotes = release->description();  // Markup; converted when shown
        }
        app.iconPath = cachedIconPath(component);
        app.screenshots = screenshotUrls(component, 4);
        app.origin = overlay ? MetadataOrigin::Overlay : MetadataOrigin::AppStream;
        apps.append(app);
    }
    // Stray files in /usr/share/swcatalog can name packages; only Arch's catalogue matches repo packages.
    osMetadataMissing = archClaimed.isEmpty();
    QSet<QString> claimed;  // Repo packages already shown through a metadata entry
    for (const auto &app : apps) for (const auto &package : app.packages) if (package.source == Source::Repo) claimed.insert(package.name);
    for (const auto &package : repoPackages) {
        if (claimed.contains(package.name)) continue;
        CatalogApp app;
        app.key = "pkg:" + package.name;
        app.name = package.name;
        app.summary = package.description;
        app.packages.append({Source::Repo, package.name, package.repo, package.version});
        apps.append(app);
    }
    for (const auto &entry : aur) {
        if (!entry.detailed() || aurClaimed.contains(entry.name)) continue;
        CatalogApp app;
        app.key = "aur:" + entry.name;
        app.name = entry.name;
        app.summary = entry.description;
        app.homepage = entry.url;
        app.packages.append({Source::Aur, entry.name, "aur", entry.version, entry.popularity});
        apps.append(app);
    }
    apps = mergeCatalog(std::move(apps), overrides, &mergeStats);
    emit catalogReady();
}

void Backend::readCommand(const QString &program, const QStringList &args,
                          std::function<void(int, QString, QString)> done, int timeout) {
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    auto delivered = std::make_shared<bool>(false);
    auto timedOut = std::make_shared<bool>(false);
    auto finish = [process, timer, done, delivered](int code, const QString &overrideError = {}) {
        if (*delivered) return;
        *delivered = true;
        timer->stop();
        const QString output = QString::fromUtf8(process->readAllStandardOutput());
        const QString error = overrideError.isEmpty() ? QString::fromUtf8(process->readAllStandardError()) : overrideError;
        done(code, output, error);
        process->deleteLater();
    };
    connect(timer, &QTimer::timeout, process, [process, timedOut] { *timedOut = true; process->kill(); });
    connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(-1, process->errorString());
    });
    connect(process, &QProcess::finished, process, [finish, timedOut](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? code : -1, *timedOut ? "The request timed out. Check your connection and retry." : QString());
    });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C.UTF-8");
    process->setProcessEnvironment(env);
    process->start(program, args);
    timer->start(timeout);
}

void Backend::load() {
    pendingSources = 4;  // AppStream pool, Flatpak versions, repos, AUR
    pool.setFlags(AppStream::Pool::Flags(AppStream::Pool::FlagLoadOsCatalog) | AppStream::Pool::FlagLoadFlatpak);
    for (const QString &dir : metadataDirs()) pool.addExtraDataLocation(dir, AppStream::Metadata::FormatStyleCatalog);
    overrides = loadMergeOverrides(metadataDirs());
    pool.loadAsync();
    readCommand("flatpak", {"remote-ls", "--cached", "--app", "--columns=application,branch,origin,version"},
        [this](int code, QString output, QString error) {
            if (code != 0) { emit issue("Cannot identify cached Flatpak sources: " + error); sourceDone(); return; }
            for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                const auto f = line.split('\t');
                if (f.size() < 4) continue;
                flatpakOrigins.insert(f[0] + '/' + f[1], f[2]);
                flatpakVersions.insert(f[0] + '/' + f[1], f[3]);
            }
            for (auto &app : catalogue) {
                app.remote = flatpakOrigins.value(app.id + '/' + app.branch);
                app.version = flatpakVersions.value(app.id + '/' + app.branch);
            }
            emit catalogueChanged();
            sourceDone();
        });
    loadRepos();
    loadAur();
    loadRankings();
    readCommand("flatpak", {"remotes", "--columns=name,options"}, [this](int code, QString output, QString error) {
        if (code != 0) { emit issue("Cannot read Flatpak sources: " + error); return; }
        userRemotes.clear(); systemRemotes.clear();
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const auto fields = line.split('\t');
            if (fields.size() < 2 || fields[1].contains("disabled")) continue;
            (fields[1].contains("user") ? userRemotes : systemRemotes).append(fields[0]);
        }
        emit catalogueChanged();
    });
    loadInstalled();
}

void Backend::loadInstalled() {
    const quint64 generation = ++installedGeneration;
    auto apps = std::make_shared<Apps>(importedAppImages(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)));
    auto pending = std::make_shared<int>(2);
    auto complete = [this, apps, pending, generation] {
        if (--*pending != 0 || generation != installedGeneration) return;
        installed = *apps;
        std::sort(installed.begin(), installed.end(), [](const App &a, const App &b) { return a.name.localeAwareCompare(b.name) < 0; });
        emit installedChanged();
    };
    readCommand("flatpak", {"list", "--app", "--columns=application,name,version,origin,installation,branch"},
        [this, apps, complete](int code, QString output, QString error) {
            if (code != 0) emit issue("Cannot list installed Flatpaks: " + error);
            else for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                const auto f = line.split('\t');
                if (f.size() < 6) continue;
                App app;
                app.id = f[0]; app.name = f[1]; app.version = f[2]; app.remote = f[3]; app.scope = f[4]; app.branch = f[5];
                app.source = "Flatpak";
                app.summary = f[3] + " · " + f[4];
                app.icon = QIcon::fromTheme(app.id, QIcon::fromTheme(app.id.section('.', -1).toLower(), QIcon::fromTheme("application-x-executable")));
                apps->append(app);
            }
            complete();
        });
    readCommand("pacman", {"-Qm"}, [this, apps, complete](int code, QString output, QString error) {
        if (code != 0 && !error.isEmpty()) emit issue("Cannot list foreign Arch packages: " + error);
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const auto f = line.split(' ', Qt::SkipEmptyParts);
            if (f.size() < 2) continue;
            App app;
            app.id = app.name = f[0]; app.version = f[1]; app.source = "AUR / local";
            app.summary = "Foreign Arch package · AUR origin is not verified";
            app.icon = QIcon::fromTheme(f[0], QIcon::fromTheme("package-x-generic"));
            apps->append(app);
        }
        complete();
    });
}

void Backend::searchAur(const QString &query, std::function<void(Apps, QString)> done) {
    if (query.trimmed().size() < 2) { done({}, {}); return; }
    QUrl url("https://aur.archlinux.org/rpc/v5/search/" + QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed())));
    QUrlQuery parameters;
    parameters.addQueryItem("by", "name-desc");
    url.setQuery(parameters);
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        Apps apps;
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = "AUR search failed: " + reply->errorString();
        else {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto root = doc.object();
            if (!root.value("results").isArray()) error = "The AUR returned an invalid response.";
            else if (root.value("type").toString() == "error") error = root.value("error").toString();
            else for (const auto &value : root.value("results").toArray()) {
                const auto item = value.toObject();
                App app;
                app.id = app.name = item.value("Name").toString();
                app.base = item.value("PackageBase").toString();
                if (!validPackageName(app.id) || !validPackageName(app.base)) continue;
                app.summary = item.value("Description").toString();
                app.version = item.value("Version").toString(); app.source = "AUR";
                app.homepage = item.value("URL").toString();
                app.icon = QIcon::fromTheme(app.id, QIcon::fromTheme("package-x-generic"));
                apps.append(app);
            }
        }
        reply->deleteLater();
        done(apps, error);
    });
}

void Backend::prepareAur(const App &app, std::function<void(QString, QString, QString)> done) {
    reviewComplete = false;
    reviewedHashes.clear();
    const quint64 generation = ++reviewGeneration;
    if (!validPackageName(app.base)) { done({}, {}, "Invalid AUR package base."); return; }
    auto temp = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/woof-store-review-XXXXXX");
    if (!temp->isValid()) { done({}, {}, "Cannot create a review folder."); return; }
    review = temp;
    const QString checkout = temp->path() + "/package";
    const QStringList gitArgs = {"-c", "core.hooksPath=/dev/null", "-c", "protocol.file.allow=never", "clone", "--depth=1", "--", "https://aur.archlinux.org/" + app.base + ".git", checkout};
    readCommand("git", gitArgs, [this, temp, checkout, generation, done](int code, QString, QString error) {
        if (generation != reviewGeneration) return;
        if (code != 0) { done({}, {}, "Cannot download AUR build files: " + error); return; }
        readCommand("git", {"-C", checkout, "rev-parse", "HEAD"}, [this, temp, checkout, generation, done](int code, QString commit, QString error) {
            if (generation != reviewGeneration) return;
            if (code != 0) { done({}, {}, error); return; }
            readCommand("git", {"-C", checkout, "ls-files", "-z"}, [this, temp, checkout, commit, generation, done](int code, QString output, QString error) {
                if (generation != reviewGeneration) return;
                if (code != 0) { done({}, {}, error); return; }
                QString text;
                QMap<QString, QByteArray> hashes;
                for (const auto &name : output.split(QChar(0), Qt::SkipEmptyParts)) {
                    const QString path = checkout + '/' + name;
                    QFileInfo info(path);
                    if (info.isSymLink() || !info.isFile() || info.size() > 256 * 1024 || name.contains(QRegularExpression("[\\x00-\\x1f]"))) {
                        done({}, {}, "This recipe contains a symlink, oversized file, or unsupported filename. Review it manually before installing."); return;
                    }
                    QFile file(path);
                    if (!file.open(QIODevice::ReadOnly)) { done({}, {}, file.errorString()); return; }
                    const auto bytes = file.readAll();
                    if (bytes.contains('\0')) { done({}, {}, "This recipe includes a binary file. The store cannot provide a complete text review."); return; }
                    if (text.size() + bytes.size() > 1024 * 1024) { done({}, {}, "The review exceeds 1 MiB. Review this package manually."); return; }
                    hashes.insert(name, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
                    text += "\n──── " + name + " ────\n" + QString::fromUtf8(bytes) + '\n';
                }
                if (!hashes.contains("PKGBUILD")) { done({}, {}, "No PKGBUILD was found."); return; }
                reviewedHashes = hashes;
                reviewComplete = true;
                done(text, commit.trimmed(), {});
            });
        });
    }, 60000);
}

QString Backend::reviewPath() const { return review ? review->path() + "/package" : QString(); }

bool Backend::reviewedFilesUnchanged(QString *error) const {
    if (!reviewComplete || !review) { *error = "Download and review the build files first."; return false; }
    QSet<QString> actual;
    QDirIterator it(reviewPath(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString name = QDir(reviewPath()).relativeFilePath(path);
        if (name.startsWith(".git/")) continue;
        actual.insert(name);
        if (it.fileInfo().isSymLink() || !reviewedHashes.contains(name) || digestFile(path) != reviewedHashes.value(name)) {
            *error = "The downloaded build files changed after review. Download and review them again."; return false;
        }
    }
    if (actual.size() != reviewedHashes.size()) { *error = "A reviewed file was removed. Download the recipe again."; return false; }
    error->clear();
    return true;
}

void Backend::loadRankings() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QList<QPair<QString, QUrl>> sources{
        {dir + "/flathub-popular.json", QUrl("https://flathub.org/api/v2/collection/popular?page=1&per_page=250")},
        {dir + "/flathub-trending.json", QUrl("https://flathub.org/api/v2/collection/trending?page=1&per_page=50")},
        {dir + "/pkgstats.json", QUrl("https://pkgstats.archlinux.de/api/packages?limit=3000&offset=0")}};
    struct State { QList<QByteArray> data{{}, {}, {}}; int pending = 3; bool offline = false; };
    auto state = std::make_shared<State>();
    auto finish = [this, state] {
        if (--state->pending) return;
        rankings = parseRankings(state->data[0], state->data[1], state->data[2]);
        rankings.offline = state->offline;
        emit rankingsReady();
    };
    for (int i = 0; i < sources.size(); ++i) {
        const QString path = sources[i].first;
        QFile saved(path);
        const QFileInfo info(path);
        if (info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600 && saved.open(QIODevice::ReadOnly)) {
            state->data[i] = saved.readAll();
            finish();
            continue;
        }
        QNetworkRequest request(sources[i].second);
        request.setTransferTimeout(20000);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, path, i, state, finish] {
            reply->deleteLater();
            QByteArray data = reply->readAll();
            if (reply->error() == QNetworkReply::NoError && QJsonDocument::fromJson(data).isObject()) {
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
            } else {
                QFile old(path);  // Offline: the last saved ranking, if any
                data = old.open(QIODevice::ReadOnly) ? old.readAll() : QByteArray();
                state->offline = true;
            }
            state->data[i] = data;
            finish();
        });
    }
}

bool Backend::loadFixture(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QDir base = QFileInfo(path).absoluteDir();
    auto local = [&base](const QString &relative) { return relative.isEmpty() ? QString() : base.absoluteFilePath(relative); };
    const QHash<QString, Source> sources{{"flatpak", Source::Flatpak}, {"repo", Source::Repo}, {"aur", Source::Aur}};
    apps.clear();
    fixtureAddons.clear();
    auto read = [&](const QJsonObject &object) {
        CatalogApp app;
        app.key = object.value("key").toString();
        app.componentId = app.key;
        app.name = object.value("name").toString();
        app.summary = object.value("summary").toString();
        app.description = object.value("description").toString();
        app.homepage = object.value("homepage").toString();
        app.iconPath = local(object.value("icon").toString());
        app.categories = object.value("categories").toVariant().toStringList();
        app.keywords = object.value("keywords").toVariant().toStringList();
        for (const auto &shot : object.value("screenshots").toArray()) app.screenshots.append(QUrl::fromLocalFile(local(shot.toString())));
        for (const auto &link : object.value("links").toArray()) app.links.append({link.toObject().value("type").toString(), link.toObject().value("url").toString()});
        for (const auto &item : object.value("packages").toArray()) {
            const auto package = item.toObject();
            app.packages.append({sources.value(package.value("source").toString()), package.value("name").toString(), package.value("repo").toString(),
                                 package.value("version").toString(), package.value("popularity").toDouble()});
        }
        const auto release = object.value("release").toObject();
        app.releaseVersion = release.value("version").toString();
        app.releaseDate = QDate::fromString(release.value("date").toString(), Qt::ISODate);
        app.releaseNotes = release.value("notes").toString();
        app.origin = object.value("origin").toString() == "none" ? MetadataOrigin::None : MetadataOrigin::AppStream;
        return app;
    };
    for (const auto &value : root.value("apps").toArray()) {
        apps.append(read(value.toObject()));
        for (const auto &addon : value.toObject().value("addons").toArray()) fixtureAddons[apps.last().key].append(read(addon.toObject()));
    }
    aur.clear();
    for (const auto &value : root.value("aur").toArray())
        aur.append({value.toObject().value("name").toString(), value.toObject().value("version").toString(), {}, {}, 0, 0});
    rankings = {};
    rankings.popular = root.value("popular").toVariant().toStringList();
    rankings.trending = root.value("trending").toVariant().toStringList();
    const auto installs = root.value("installs").toObject();
    for (auto it = installs.begin(); it != installs.end(); ++it) rankings.flathubInstalls.insert(it.key(), it.value().toDouble());
    const auto stats = root.value("pkgstats").toObject();
    for (auto it = stats.begin(); it != stats.end(); ++it) rankings.archPopularity.insert(it.key(), it.value().toDouble());
    emit catalogReady();
    emit rankingsReady();
    return !apps.isEmpty();
}

void Backend::fetchImage(const QUrl &url, QObject *context, std::function<void(QPixmap)> done) {
    const QString key = url.toString();
    QPixmap image;
    if (QPixmapCache::find(key, &image)) { done(image); return; }
    auto store = [key](QImage decoded) {
        if (decoded.width() > 1280) decoded = decoded.scaledToWidth(1280, Qt::SmoothTransformation);  // Caps memory per screenshot
        const QPixmap pixmap = QPixmap::fromImage(decoded);
        if (!pixmap.isNull()) QPixmapCache::insert(key, pixmap);
        return pixmap;
    };
    if (url.isLocalFile()) { done(store(QImage(url.toLocalFile()))); return; }
    if (url.scheme() != "https" && url.scheme() != "http") { done({}); return; }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(30000);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [reply, store, done] {
        done(reply->error() == QNetworkReply::NoError ? store(QImage::fromData(reply->readAll())) : QPixmap());
    });
}

QList<CatalogApp> Backend::addons(const CatalogApp &app) const {
    if (!fixtureAddons.isEmpty()) return fixtureAddons.value(app.key);
    QList<CatalogApp> result;
    if (app.componentId.isEmpty()) return result;
    QSet<QString> seen;
    for (const auto &component : pool.componentsByExtends(app.componentId).toList()) {
        CatalogApp addon;
        addon.key = normalizeComponentId(component.id());
        if (seen.contains(addon.key)) continue;  // Listed once per Flathub remote
        seen.insert(addon.key);
        addon.componentId = component.id();
        addon.name = component.name();
        addon.summary = component.summary();
        addon.description = component.description();
        addon.iconPath = cachedIconPath(component);
        addon.origin = MetadataOrigin::AppStream;
        const auto bundle = component.bundle(AppStream::Bundle::KindFlatpak);
        if (!bundle.isEmpty()) addon.packages.append({Source::Flatpak, flatpakRef(bundle).first, component.origin(), {}});
        for (const QString &name : component.packageNames()) addon.packages.append({Source::Repo, name, {}, {}});
        result.append(addon);
    }
    std::sort(result.begin(), result.end(), [](const CatalogApp &a, const CatalogApp &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return result;
}
