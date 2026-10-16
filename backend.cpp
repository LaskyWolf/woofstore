#include "backend.h"
#include <AppStreamQt/bundle.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/image.h>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

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
    const QString appDir = dataRoot + "/CachyStore/appimages";
    const QString desktopDir = dataRoot + "/applications";
    if (dataRoot.contains('\n') || dataRoot.contains('\r')) return fail("The application data path contains unsupported characters.");
    if (!QDir().mkpath(appDir) || !QDir().mkpath(desktopDir)) return fail("Cannot create the app or launcher folder.");
    const QString dest = appDir + '/' + id + ".AppImage";
    const QString launcher = desktopDir + "/cachy-store-" + id + ".desktop";
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
    const QByteArray entry = QString("[Desktop Entry]\nType=Application\nName=%1\nExec=%2\nIcon=application-x-executable\nTerminal=false\nCategories=Utility;\nX-CachyStore-AppImage=%3\n")
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
    const QDir folder(dataRoot + "/CachyStore/appimages");
    for (const QString &file : folder.entryList({"*.AppImage"}, QDir::Files)) {
        App app;
        app.id = QFileInfo(file).completeBaseName();
        app.path = folder.absoluteFilePath(file);
        app.name = app.id;
        QFile launcher(dataRoot + "/applications/cachy-store-" + app.id + ".desktop");
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

Backend::Backend(QObject *parent) : QObject(parent), pool(this), network(this) {
    connect(&pool, &AppStream::Pool::loadFinished, this, [this](bool ok) {
        if (!ok) emit issue("Cannot load the Flatpak catalogue: " + pool.lastError());
        catalogue.clear();
        QSet<QString> seen;
        for (const auto &component : pool.componentsByKind(AppStream::Component::KindDesktopApp)) {
            const auto bundle = component.bundle(AppStream::Bundle::KindFlatpak);
            if (bundle.isEmpty()) continue;
            const QStringList ref = bundle.id().split('/');
            const QString id = ref.value(0) == "app" ? ref.value(1) : ref.value(0);
            const QString branch = ref.value(0) == "app" ? ref.value(3) : ref.value(2);
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
            // Resolve cached icons here; theme lookups are deferred to visible cards.
            for (const auto &icon : component.icons()) {
                if (!icon.filename().isEmpty() && QFileInfo::exists(icon.filename())) {
                    app.icon = QIcon(icon.filename());
                    if (icon.width() >= 64) break;
                }
            }
            for (const auto &shot : component.screenshotsAll()) {
                const auto image = shot.image(900, 500, 1);
                if (image && image->url().scheme() == "https") { app.screenshot = image->url(); break; }
            }
            catalogue.append(app);
        }
        std::sort(catalogue.begin(), catalogue.end(), [](const App &a, const App &b) { return a.name.localeAwareCompare(b.name) < 0; });
        emit catalogueChanged();
    });
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
    pool.setFlags(AppStream::Pool::FlagLoadFlatpak);
    pool.loadAsync();
    readCommand("flatpak", {"remote-ls", "--cached", "--app", "--columns=application,branch,origin,version"},
        [this](int code, QString output, QString error) {
            if (code != 0) { emit issue("Cannot identify cached Flatpak sources: " + error); return; }
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
        });
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
    auto temp = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/cachy-store-review-XXXXXX");
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
