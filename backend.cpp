#include "backend.h"
#include <AppStreamQt/bundle.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/launchable.h>
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
#include <QXmlStreamReader>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <malloc.h>

QString plainDescription(const QString &html) {
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QString appImageUpdateInfo(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = file.read(64);
    if (head.size() < 64 || head.left(4) != QByteArray("\x7f" "ELF", 4) || head[4] != 2 || head[5] != 1) return {};  // 64-bit, little endian
    auto number = [](const QByteArray &bytes, int at, int size) { quint64 value = 0; for (int i = size - 1; i >= 0; --i) value = (value << 8) | quint8(bytes[at + i]); return value; };
    const quint64 tableAt = number(head, 0x28, 8), entry = number(head, 0x3A, 2), count = number(head, 0x3C, 2), namesIndex = number(head, 0x3E, 2);
    if (!tableAt || entry < 64 || !count || count > 256 || namesIndex >= count || !file.seek(tableAt)) return {};
    const QByteArray table = file.read(qint64(entry * count));
    if (table.size() != qint64(entry * count)) return {};
    auto bytesOf = [&](quint64 index, qint64 limit) {
        const quint64 at = number(table, int(index * entry + 0x18), 8), size = number(table, int(index * entry + 0x20), 8);
        return size <= quint64(limit) && file.seek(at) ? file.read(qint64(size)) : QByteArray();
    };
    const QByteArray names = bytesOf(namesIndex, 1 << 20);
    for (quint64 i = 0; i < count; ++i) {
        const quint64 nameAt = number(table, int(i * entry), 4);
        if (nameAt >= quint64(names.size()) || QByteArray(names.constData() + nameAt) != ".upd_info") continue;
        return QString::fromUtf8(QByteArray(bytesOf(i, 4096).constData())).trimmed();  // up to the first NUL
    }
    return {};
}

std::optional<UpdateInfo> parseUpdateInfo(const QString &info) {
    const QStringList parts = info.split('|');
    UpdateInfo result;
    if (parts.value(0) == "zsync" && parts.size() >= 2 && parts[1].startsWith("https://")) { result.zsyncUrl = parts[1]; return result; }
    if (parts.value(0) == "gh-releases-zsync" && parts.size() >= 5 && validPackageName(parts[1]) && validPackageName(parts[2]) && !parts[3].isEmpty() && !parts[4].isEmpty()) {
        result.owner = parts[1]; result.repo = parts[2]; result.tag = parts[3]; result.pattern = parts[4];
        return result;
    }
    return std::nullopt;
}

QString zsyncSha1(const QByteArray &header) {
    static const QRegularExpression line("(?m)^SHA-1:\\s*([0-9a-fA-F]{40})\\s*$");
    const auto match = line.match(QString::fromLatin1(header.left(4096)));
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

ReleaseAsset pickReleaseAsset(const QByteArray &releaseJson, const QString &pattern) {
    const auto release = QJsonDocument::fromJson(releaseJson).object();
    const QRegularExpression wanted(QRegularExpression::wildcardToRegularExpression(pattern));
    QMap<QString, QString> urls;
    for (const auto &value : release.value("assets").toArray()) urls.insert(value.toObject().value("name").toString(), value.toObject().value("browser_download_url").toString());
    // The publisher's pattern names the .zsync file; the AppImage itself has the same name without ".zsync".
    for (auto it = urls.cbegin(); it != urls.cend(); ++it) {
        if (!it.key().endsWith(".zsync") || !wanted.match(it.key()).hasMatch()) continue;
        const QString imageName = it.key().chopped(6);
        if (urls.contains(imageName)) return {urls.value(imageName), it.value(), release.value("tag_name").toString()};
    }
    return {};
}

// The first picture in a piece of HTML.
static QString firstImage(const QString &html) {
    static const QRegularExpression tag("<img[^>]+src=\"(https?://[^\"]+)\"");
    const auto match = tag.match(html);
    return match.hasMatch() ? match.captured(1) : QString();
}
// WordPress sites link small copies ("name-300x127.jpg"); the original has no size in its name.
static QString fullSizeImage(QString url) {
    static const QRegularExpression size("-\\d+x\\d+(\\.(?:jpe?g|png|webp))$");
    if (url.contains("/wp-content/uploads/")) url.replace(size, "\\1");
    return url;
}

QList<NewsItem> parseFeed(const QByteArray &xml, const QString &source, bool important) {
    QList<NewsItem> items;
    QXmlStreamReader reader(xml);
    NewsItem item; bool inItem = false;
    auto finish = [&] {
        if (item.image.isEmpty()) item.image = firstImage(item.summary);
        item.image = fullSizeImage(item.image);
        item.summary = plainDescription(item.summary).remove(QChar::ObjectReplacementCharacter).simplified();
        if (item.summary.size() > 220) item.summary = item.summary.left(217).trimmed() + "…";
        if (!item.title.isEmpty() && item.url.startsWith("http")) items.append(item);
    };
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == "item") { inItem = true; item = {}; item.source = source; item.important = important; }
            else if (inItem && name == "title") item.title = reader.readElementText().simplified();
            else if (inItem && name == "link") item.url = reader.readElementText().trimmed();
            else if (inItem && name == "description") item.summary = reader.readElementText();
            else if (inItem && name == "encoded") { const QString html = reader.readElementText(); if (item.image.isEmpty()) item.image = firstImage(html); }
            else if (inItem && item.image.isEmpty() && (name == "enclosure" || name == "content" || name == "thumbnail")) {
                const auto attributes = reader.attributes();
                const QString url = attributes.value("url").toString(), type = attributes.value("type").toString(), medium = attributes.value("medium").toString();
                const bool picture = name == "thumbnail" || type.startsWith("image/") || medium == "image" || (type.isEmpty() && url.contains(QRegularExpression("\\.(jpe?g|png|webp)(\\?|$)")));
                if (picture && url.startsWith("http")) item.image = url;
            }
            else if (inItem && name == "pubDate") item.date = QDateTime::fromString(reader.readElementText().trimmed(), Qt::RFC2822Date);
        } else if (reader.isEndElement() && reader.name() == QLatin1String("item")) { inItem = false; finish(); }
    }
    return items;
}

QList<NewsItem> parseCachyBlog(const QString &html) {
    // Each post: <time datetime="..">date</time> ... <h2><a href="/blog/slug">Title</a></h2> ... <p>Summary</p>
    static const QRegularExpression post("<time[^>]*>([^<]*)</time>.*?<h2[^>]*>\\s*<a[^>]*href=\"(/blog/[^\"]+)\"[^>]*>([^<]+)</a>\\s*</h2>.*?<p[^>]*>([^<]*)</p>", QRegularExpression::DotMatchesEverythingOption);
    QList<NewsItem> items;
    for (auto it = post.globalMatch(html); it.hasNext();) {
        const auto match = it.next();
        NewsItem item; item.source = "CachyOS"; item.important = true;
        item.date = QLocale::c().toDateTime(match.captured(1).trimmed(), "MMM d, yyyy");
        item.url = "https://cachyos.org" + match.captured(2); item.title = plainDescription(match.captured(3)).simplified(); item.summary = plainDescription(match.captured(4)).simplified();
        items.append(item);
    }
    return items;
}

QList<Orphan> parseOrphans(const QString &text) {
    QList<Orphan> result;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const auto fields = line.trimmed().split('\t');
        if (validPackageName(fields[0])) result.append({fields[0], fields.value(1).trimmed()});
    }
    return result;
}

void Backend::orphans(std::function<void(QList<Orphan>)> done) {
    // pacman -Qdtq: dependencies nothing requires or optionally requires. expac adds the size when it is installed.
    readCommand("bash", {"-c", "pacman -Qdtq | while read -r n; do printf '%s\\t%s\\n' \"$n\" \"$(expac -H M '%m' \"$n\" 2>/dev/null)\"; done"},
                [done](int, QString output, QString) { done(parseOrphans(output)); });
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

QString updateScript(bool system, bool aur, bool devel, bool flatpak, bool appImages, QStringList *stepNames) {
    QStringList names; QString script = "fail=0\nexe=\"$1\"; shift\n";
    auto section = [&](const QString &name) { names << name; script += "echo '==> " + name + "'\n"; };
    if (system) {
        section("System packages");
        script += "pkexec pacman -Syu --noconfirm || { echo '==> The system upgrade failed, so nothing else was updated.'; exit 1; }\n";
    }
    if (aur) {
        section("AUR packages");
        script += QString("if command -v paru >/dev/null; then paru -Sua --noconfirm --skipreview%1 || { echo '==> The AUR update failed.'; fail=1; }; else echo 'paru is not installed.'; fail=1; fi\n").arg(devel ? " --devel" : "");
    }
    if (flatpak) {
        section("Flatpak apps");
        script += "flatpak update -y --noninteractive || { echo '==> The Flatpak update failed.'; fail=1; }\n";
    }
    if (appImages) {
        section("AppImages");
        // Each AppImage downloads under its old name, is imported as the new copy, and the old copy goes away.
        script += "tmp=$(mktemp -d); trap 'rm -rf \"$tmp\"' EXIT\n"
                  "for item in \"$@\"; do\n"
                  "  IFS=$'\\t' read -r id url name <<<\"$item\"\n"
                  "  echo \"Downloading $name\"; mkdir -p \"$tmp/$id\"; file=\"$tmp/$id/${name//\\//_}.AppImage\"\n"
                  "  if curl -fsSL -o \"$file\" \"$url\" && \"$exe\" --import-appimage \"$file\" --replaces \"$id\"; then echo \"Updated $name\"; else echo \"Could not update $name\"; fail=1; fi\n"
                  "done\n";
    }
    if (system) {
        section("Orphan packages");  // Only reported: removing them is the user's call
        script += "orphans=$(pacman -Qdtq 2>/dev/null); if [ -n \"$orphans\" ]; then echo 'Orphan packages (nothing needs them any more):'; echo \"$orphans\"; echo 'Remove them with: sudo pacman -Rns $(pacman -Qdtq)'; else echo 'No orphan packages.'; fi\n";
    }
    script += "exit $fail\n";
    if (stepNames) *stepNames = names;
    return script;
}

QString replaceAppImage(const QString &file, const QString &oldId, const QString &dataRoot, QString *error) {
    if (!QRegularExpression("^[0-9a-f]{64}$").match(oldId).hasMatch()) { if (error) *error = "This is not an imported AppImage."; return {}; }
    const QString dest = importAppImage(file, dataRoot, error);
    if (dest.isEmpty()) return {};
    QFile::remove(dataRoot + "/WoofStore/appimages/" + oldId + ".AppImage");
    QFile::remove(dataRoot + "/applications/woof-store-" + oldId + ".desktop");
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
        app.source = "AppImage"; app.launcher = dataRoot + "/applications/woof-store-" + app.id + ".desktop";
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
        app.desktopId = component.launchable(AppStream::Launchable::KindDesktopId).entries().value(0);
        if (const auto release = component.releasesPlain().indexSafe(0)) {
            app.releaseVersion = release->version();
            app.releaseDate = release->timestamp().date();
            app.releaseNotes = release->description();  // Markup; converted when shown
            for (int i = 1; i < 8; ++i) if (const auto older = component.releasesPlain().indexSafe(i))
                app.olderReleases.append({older->version(), older->timestamp().date(), older->description().left(4000)});
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

std::optional<Launcher> parseLauncher(const QString &path, const QByteArray &text, const QString &language) {
    Launcher launcher; launcher.path = path;
    QString name, comment, localName, localComment, type, onlyShowIn; bool hidden = false, inEntry = false;
    const QString shortLanguage = language.section('_', 0, 0);
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith('[')) { inEntry = line == "[Desktop Entry]"; continue; }
        if (!inEntry || line.startsWith('#')) continue;
        const int equals = line.indexOf('=');
        if (equals < 1) continue;
        const QString key = line.left(equals).trimmed(), value = line.mid(equals + 1).trimmed();
        if (key == "Name") name = value;
        else if (key == "Name[" + language + "]" || (key == "Name[" + shortLanguage + "]" && localName.isEmpty())) localName = value;
        else if (key == "Comment") comment = value;
        else if (key == "Comment[" + language + "]" || (key == "Comment[" + shortLanguage + "]" && localComment.isEmpty())) localComment = value;
        else if (key == "Icon") launcher.icon = value;
        else if (key == "Exec") launcher.exec = value;
        else if (key == "Type") type = value;
        else if (key == "OnlyShowIn") onlyShowIn = value;
        else if (key == "NoDisplay" || key == "Hidden") hidden |= value.compare("true", Qt::CaseInsensitive) == 0;
    }
    if (hidden || type != "Application" || launcher.exec.isEmpty()) return std::nullopt;
    if (!onlyShowIn.isEmpty() && !onlyShowIn.split(';').contains("KDE")) return std::nullopt;
    if (launcher.exec.contains("steam://rungameid")) return std::nullopt;  // Games are managed by Steam
    launcher.name = localName.isEmpty() ? name : localName;
    launcher.comment = localComment.isEmpty() ? comment : localComment;
    if (launcher.name.isEmpty()) return std::nullopt;
    launcher.id = QFileInfo(path).completeBaseName();
    launcher.flatpak = path.contains("/flatpak/exports/");
    return launcher;
}

QList<Launcher> scanLaunchers(const QStringList &dirs, const QString &language) {
    QList<Launcher> result; QSet<QString> seen;
    for (const QString &dir : dirs) {
        QDirIterator files(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            // Like the menu: the first folder that has an ID wins, so a user entry hides the system one.
            const QString id = QDir(dir).relativeFilePath(path).replace('/', '-');
            if (seen.contains(id)) continue;
            seen.insert(id);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) continue;
            if (auto launcher = parseLauncher(path, file.readAll(), language)) result.append(*launcher);
        }
    }
    return result;
}

static QIcon launcherIcon(const Launcher &launcher, const QString &fallback) {
    if (launcher.icon.startsWith('/')) return QFileInfo::exists(launcher.icon) ? QIcon(launcher.icon) : QIcon::fromTheme(fallback);
    return QIcon::fromTheme(launcher.icon, QIcon::fromTheme(launcher.id, QIcon::fromTheme(fallback)));
}

// The program an AppImage launcher starts, without quotes and arguments.
static QString launcherProgram(const QString &exec) {
    const QString trimmed = exec.trimmed();
    if (trimmed.startsWith('"')) return trimmed.mid(1).section('"', 0, 0);
    return trimmed.section(' ', 0, 0);
}

void Backend::loadInstalled() {
    const quint64 generation = ++installedGeneration;
    auto apps = std::make_shared<Apps>(importedAppImages(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)));
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &flatpakDir : {QString("/var/lib/flatpak/exports/share/applications"), QDir::homePath() + "/.local/share/flatpak/exports/share/applications"})
        if (!dirs.contains(flatpakDir)) dirs << flatpakDir;
    const auto launchers = std::make_shared<QList<Launcher>>(scanLaunchers(dirs, QLocale().name()));
    auto owners = std::make_shared<QHash<QString, QPair<QString, QString>>>();  // launcher path -> package, version
    auto foreign = std::make_shared<QSet<QString>>();
    auto pending = std::make_shared<int>(4);
    auto complete = [this, apps, launchers, owners, foreign, pending, generation] {
        if (--*pending != 0 || generation != installedGeneration) return;
        // Like the menu: one row per application. A package with several launchers gets one row under its own name.
        QMap<QString, QList<const Launcher *>> byPackage;
        QSet<QString> flatpakIds; for (const auto &app : *apps) if (app.source == "Flatpak") flatpakIds.insert(app.id);
        for (const auto &launcher : *launchers) {
            if (launcher.flatpak) {
                for (auto &app : *apps) if (app.source == "Flatpak" && app.id == launcher.id) { app.summary = launcher.comment; app.launcher = launcher.path; app.icon = launcherIcon(launcher, "application-x-executable"); }
                continue;
            }
            if (launcher.id.startsWith("woof-store-")) continue;  // Our own AppImage launchers; the imported list covers them
            if (owners->contains(launcher.path)) { byPackage[owners->value(launcher.path).first].append(&launcher); continue; }
            App app; app.id = launcher.id; app.name = launcher.name; app.summary = launcher.comment; app.scope = launcher.path.startsWith(QDir::homePath()) ? "user" : "system"; app.launcher = launcher.path;
            app.icon = launcherIcon(launcher, "application-x-executable");
            if (launcher.exec.contains(".appimage", Qt::CaseInsensitive)) { app.source = "AppImage"; app.path = launcherProgram(launcher.exec); }
            else app.source = "Other";
            apps->append(app);
        }
        for (auto it = byPackage.cbegin(); it != byPackage.cend(); ++it) {
            const Launcher &first = *it.value().first();
            App app; app.id = it.key(); app.version = owners->value(first.path).second; app.scope = "system";
            app.source = foreign->contains(it.key()) ? "AUR / local" : "Arch";
            app.name = it.value().size() == 1 ? first.name : it.key(); app.summary = it.value().size() == 1 ? first.comment : QString();
            app.icon = launcherIcon(first, "package-x-generic"); app.launcher = first.path;
            apps->append(app);
        }
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
                app.icon = QIcon::fromTheme(app.id, QIcon::fromTheme(app.id.section('.', -1).toLower(), QIcon::fromTheme("application-x-executable")));
                apps->append(app);
            }
            complete();
        });
    readCommand("pacman", {"-Qqn"}, [this, complete](int code, QString output, QString) {
        nativePackages.clear();
        if (code == 0) for (const QString &name : output.split('\n', Qt::SkipEmptyParts)) nativePackages.insert(name);
        complete();
    });
    readCommand("pacman", {"-Qqm"}, [this, foreign, complete](int code, QString output, QString error) {
        if (code != 0 && !error.isEmpty()) emit issue("Cannot list foreign Arch packages: " + error);
        for (const QString &name : output.split('\n', Qt::SkipEmptyParts)) foreign->insert(name.trimmed());
        complete();
    });
    // Which package owns each system launcher; files that no package owns make pacman print an error, which is fine.
    QStringList paths;
    for (const auto &launcher : *launchers) if (!launcher.flatpak && !launcher.path.startsWith(QDir::homePath())) paths << launcher.path;
    if (paths.isEmpty()) { complete(); return; }
    readCommand("pacman", QStringList{"-Qo"} + paths, [owners, complete](int, QString output, QString) {
        static const QRegularExpression owned("^(.+) is owned by (\\S+) (\\S+)$");
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const auto match = owned.match(line.trimmed());
            if (match.hasMatch()) owners->insert(match.captured(1), {match.captured(2), match.captured(3)});
        }
        complete();
    });
}

QPair<QString, QString> versionDiff(const QString &from, const QString &to) {
    auto wrap = [](const QString &text, const char *color) { return text.isEmpty() ? QString() : QString("<span style=\"color:%1; font-weight:600\">%2</span>").arg(color, text.toHtmlEscaped()); };
    int prefix = 0;
    while (prefix < from.size() && prefix < to.size() && from[prefix] == to[prefix]) ++prefix;
    // Do not cut a number in half: step back to the start of the version part that differs.
    auto continues = [&](int at) { return (at < from.size() && from[at].isLetterOrNumber()) || (at < to.size() && to[at].isLetterOrNumber()); };
    while (prefix > 0 && from[prefix - 1].isLetterOrNumber() && continues(prefix)) --prefix;
    // Everything from the first difference to the end is coloured, so "2.5.2-1.1" and "2.5.3-1.1" differ from the "2" on.
    const QString head = from.left(prefix).toHtmlEscaped();
    return {head + wrap(from.mid(prefix), "#da4453"), head + wrap(to.mid(prefix), "#27ae60")};
}

QList<UpdateEntry> parseArchUpdates(const QString &text, const QString &source) {
    static const QRegularExpression line("^(\\S+)\\s+(\\S+)\\s+->\\s+(\\S+)$");
    QList<UpdateEntry> result;
    for (const QString &row : text.split('\n', Qt::SkipEmptyParts)) {
        const auto match = line.match(row.trimmed());
        if (match.hasMatch()) result.append({match.captured(1), match.captured(1), match.captured(2), match.captured(3), source, {}});
    }
    return result;
}

QList<UpdateEntry> parseFlatpakUpdates(const QString &text, const QHash<QString, QString> &installedVersions) {
    QList<UpdateEntry> result; QSet<QString> seen;
    for (const QString &row : text.split('\n', Qt::SkipEmptyParts)) {
        const auto f = row.split('\t');
        if (f.size() < 3 || f[0].isEmpty() || seen.contains(f[0])) continue;
        seen.insert(f[0]);
        // Runtimes (GNOME Platform, KDE Platform…) have no version of their own: the branch tells which one.
        const QString to = !f[2].isEmpty() ? f[2] : f.value(3);
        result.append({f[0], f[1].isEmpty() ? f[0] : f[1], installedVersions.value(f[0]), to.isEmpty() ? "newer" : to, "Flatpak", {}});
    }
    return result;
}

void Backend::refresh() {
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/http").removeRecursively();
    loadRankings();
    loadInstalled();
}

void Backend::checkUpdates(std::function<void(QList<UpdateEntry>, QStringList)> done) {
    auto found = std::make_shared<QList<UpdateEntry>>();
    auto problems = std::make_shared<QStringList>();
    auto pending = std::make_shared<int>(4);
    auto finish = [found, problems, pending, done] { if (--*pending == 0) done(*found, *problems); };
    QHash<QString, QString> versions; for (const auto &app : installed) if (app.source == "Flatpak") versions.insert(app.id, app.version);
    // Exit codes: checkupdates 2 and paru 1 mean "nothing to update"; a missing tool is skipped.
    readCommand("checkupdates", {}, [found, problems, finish](int code, QString output, QString error) {
        if (code == 0) *found += parseArchUpdates(output, "Arch");
        else if (code != 2 && code != -1) problems->append("Arch packages: " + error.trimmed());
        finish();
    }, 120000);
    checkAppImages([found, problems, finish](QList<UpdateEntry> entries, QStringList errors) { *found += entries; *problems += errors; finish(); });
    readCommand("paru", {"-Qua"}, [found, finish](int code, QString output, QString) {
        if (code == 0) *found += parseArchUpdates(output, "AUR");
        finish();
    }, 120000);
    readCommand("flatpak", {"remote-ls", "--updates", "--columns=application,name,version,branch"}, [found, problems, finish, versions](int code, QString output, QString error) {
        if (code == 0) *found += parseFlatpakUpdates(output, versions);
        else if (code != -1) problems->append("Flatpak: " + error.trimmed());
        finish();
    }, 120000);
}

void Backend::fetch(const QUrl &url, std::function<void(QByteArray, QString)> done, qint64 firstBytes) {
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);  // Feeds move: follow http(s) redirects
    request.setRawHeader("User-Agent", "WoofStore");
    request.setRawHeader("Accept", "application/vnd.github+json, application/xml, text/html, */*");
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done, firstBytes] {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (firstBytes > 0) data = data.left(firstBytes);
        done(data, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
    });
}

// AppImages that publish update information are compared by checksum with the newest file the publisher lists.
void Backend::checkAppImages(std::function<void(QList<UpdateEntry>, QStringList)> done) {
    auto found = std::make_shared<QList<UpdateEntry>>();
    auto problems = std::make_shared<QStringList>();
    Apps images; for (const auto &app : installed) if (app.source == "AppImage" && !app.path.isEmpty()) images.append(app);
    if (images.isEmpty() || fixtureMode) { done({}, {}); return; }
    auto pending = std::make_shared<int>(images.size());
    auto finish = [found, problems, pending, done] { if (--*pending == 0) done(*found, *problems); };
    for (const App &app : images) {
        struct Local { QString info, sha1; };
        auto *watcher = new QFutureWatcher<Local>(this);
        connect(watcher, &QFutureWatcher<Local>::finished, this, [=] {
            const Local local = watcher->result(); watcher->deleteLater();
            const auto info = parseUpdateInfo(local.info);
            if (!info) { finish(); return; }  // No update information: nothing to compare
            auto compare = [=](QString zsyncUrl, QString downloadUrl, QString version) {
                fetch(QUrl(zsyncUrl), [=](QByteArray header, QString error) {
                    const QString newest = zsyncSha1(header);
                    if (newest.isEmpty()) problems->append(app.name + ": " + (error.isEmpty() ? "no checksum in the update file" : error));
                    else if (newest != local.sha1) found->append({app.id, app.name, {}, version.isEmpty() ? "newer" : version, "AppImage", downloadUrl});
                    finish();
                }, 4096);
            };
            if (!info->zsyncUrl.isEmpty()) {
                QString download = info->zsyncUrl; download.chop(6);  // name.AppImage.zsync -> name.AppImage
                compare(info->zsyncUrl, download, {});
                return;
            }
            const QString release = info->tag == "latest" ? "latest" : "tags/" + info->tag;
            fetch(QUrl(QString("https://api.github.com/repos/%1/%2/releases/%3").arg(info->owner, info->repo, release)), [=](QByteArray json, QString error) {
                const ReleaseAsset asset = pickReleaseAsset(json, info->pattern);
                if (asset.url.isEmpty()) { problems->append(app.name + ": " + (error.isEmpty() ? "no matching release file" : error)); finish(); return; }
                compare(asset.zsyncUrl, asset.url, asset.version);
            });
        });
        const QString path = app.path;
        watcher->setFuture(QtConcurrent::run([path] {
            Local local; local.info = appImageUpdateInfo(path);
            if (local.info.isEmpty()) return local;
            QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha1);
            if (file.open(QIODevice::ReadOnly) && hash.addData(&file)) local.sha1 = QString::fromLatin1(hash.result().toHex());
            return local;
        }));
    }
}

void Backend::loadNews(std::function<void(QList<NewsItem>, QStringList)> done) {
    struct Source { QString name, url; bool important, html; };
    const QList<Source> sources = {
        {"Arch Linux", "https://archlinux.org/feeds/news/", true, false},
        {"CachyOS", "https://cachyos.org/blog/", true, true},
        {"Linux kernel", "https://www.kernel.org/feeds/kdist.xml", false, false},
        {"KDE", "https://kde.org/announcements/index.xml", false, false},
        {"LWN.net", "https://lwn.net/headlines/rss", false, false},
        {"9to5Linux", "https://9to5linux.com/feed", false, false},
        {"Phoronix", "https://www.phoronix.com/rss.php", false, false},
        {"GamingOnLinux", "https://www.gamingonlinux.com/article_rss.php", false, false},
        {"OMG! Ubuntu", "https://www.omgubuntu.co.uk/feed", false, false},
        {"Fedora Magazine", "https://fedoramagazine.org/feed/", false, false},
        {"Linuxiac", "https://linuxiac.com/feed/", false, false},
        {"It's FOSS", "https://itsfoss.com/news/rss/", false, false},
    };
    auto items = std::make_shared<QList<NewsItem>>();
    auto problems = std::make_shared<QStringList>();
    auto pending = std::make_shared<int>(sources.size());
    for (const Source &source : sources) {
        fetch(QUrl(source.url), [=](QByteArray data, QString error) {
            const auto parsed = source.html ? parseCachyBlog(QString::fromUtf8(data)) : parseFeed(data, source.name, source.important);
            if (parsed.isEmpty()) problems->append(source.name + (error.isEmpty() ? ": nothing found" : ": " + error));
            *items += parsed.mid(0, 12);
            if (--*pending > 0) return;
            std::stable_sort(items->begin(), items->end(), [](const NewsItem &a, const NewsItem &b) { return a.date > b.date; });
            done(*items, *problems);
        });
    }
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
    // Not /tmp: it is often a small RAM disk, and big packages (a browser) need gigabytes to download and build.
    const QString buildRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/aur-build";
    QDir().mkpath(buildRoot);
    auto temp = std::make_shared<QTemporaryDir>(buildRoot + "/review-XXXXXX");
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

bool reviewUnchanged(const ReviewSnapshot &snapshot, QString *error) {
    if (!snapshot.dir) { *error = "Download and review the build files first."; return false; }
    QSet<QString> actual;
    const QString root = snapshot.path();
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString name = QDir(root).relativeFilePath(path);
        if (name.startsWith(".git/")) continue;
        actual.insert(name);
        if (it.fileInfo().isSymLink() || !snapshot.hashes.contains(name) || digestFile(path) != snapshot.hashes.value(name)) {
            *error = "The downloaded build files changed after review. Download and review them again."; return false;
        }
    }
    if (actual.size() != snapshot.hashes.size()) { *error = "A reviewed file was removed. Download the recipe again."; return false; }
    error->clear();
    return true;
}

bool Backend::reviewedFilesUnchanged(QString *error) const {
    if (!reviewComplete) { *error = "Download and review the build files first."; return false; }
    return reviewUnchanged(reviewSnapshot(), error);
}

void Backend::missingFromRepos(const QStringList &dependencies, std::function<void(QStringList)> done) {
    if (dependencies.isEmpty()) { done({}); return; }
    // pacman -Sp prints the download URL when the dependency (a name, a provided name or a versioned one) can be installed from a repository.
    readCommand("bash", QStringList{"-c", "for d; do pacman -Sp -- \"$d\" >/dev/null 2>&1 || echo \"$d\"; done", "woof-store-deps"} + dependencies,
                [done](int, QString output, QString) { done(output.split('\n', Qt::SkipEmptyParts)); }, 60000);
}

void Backend::loadRankings() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QList<QPair<QString, QUrl>> sources{
        {dir + "/flathub-popular.json", QUrl("https://flathub.org/api/v2/collection/popular?page=1&per_page=250")},
        {dir + "/flathub-trending.json", QUrl("https://flathub.org/api/v2/collection/trending?page=1&per_page=50")},
        {dir + "/pkgstats.json", QUrl("https://pkgstats.archlinux.de/api/packages?limit=3000&offset=0")},
        {dir + "/flathub-added.json", QUrl("https://flathub.org/api/v2/collection/recently-added?page=1&per_page=60")},
        {dir + "/flathub-updated.json", QUrl("https://flathub.org/api/v2/collection/recently-updated?page=1&per_page=60")}};
    struct State { QList<QByteArray> data{{}, {}, {}, {}, {}}; int pending = 5; bool offline = false; };
    auto state = std::make_shared<State>();
    auto finish = [this, state] {
        if (--state->pending) return;
        rankings = parseRankings(state->data[0], state->data[1], state->data[2], state->data[3], state->data[4]);
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
    fixtureMode = true;
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
    rankings.added = root.value("added").toVariant().toStringList();
    rankings.updated = root.value("updated").toVariant().toStringList();
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

void Backend::aurInfo(const QString &name, std::function<void(AurInfo, QString)> done) {
    if (!validPackageName(name)) { done({}, "Invalid AUR package name."); return; }
    QUrl url("https://aur.archlinux.org/rpc/v5/info");
    QUrlQuery query; query.addQueryItem("arg[]", name); url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done, name] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done({}, "Cannot reach the AUR: " + reply->errorString()); return; }
        bool ok = false;
        const AurInfo info = parseAurInfo(reply->readAll(), &ok);
        if (!ok || !validPackageName(info.base)) { done({}, "The AUR does not list a package named " + name + "."); return; }
        done(info, {});
    });
}

bool Backend::installedPackage(const Package &package) const {
    if (package.source == Source::Repo) return nativePackages.contains(package.name);
    const QString kind = package.source == Source::Flatpak ? "Flatpak" : "AUR / local";
    for (const auto &app : installed) if (app.source == kind && app.id == package.name) return true;
    return false;
}

void Backend::cachedGet(const QUrl &url, std::function<void(QByteArray)> done) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/http";
    QDir().mkpath(dir);
    const QString path = dir + '/' + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex()) + ".json";
    QFile saved(path);
    if (QFileInfo(path).exists() && QFileInfo(path).lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600 && saved.open(QIODevice::ReadOnly)) { done(saved.readAll()); return; }
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, path, done] {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && QJsonDocument::fromJson(data).isObject()) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
        } else {
            QFile old(path);
            data = old.open(QIODevice::ReadOnly) ? old.readAll() : QByteArray();
        }
        done(data);
    });
}

void Backend::ratings(const CatalogApp &app, std::function<void(Ratings)> done) {
    QString flatpakId; double repoPercent = -1; QString aurName;
    for (const auto &package : app.packages) {
        if (package.source == Source::Flatpak && flatpakId.isEmpty()) flatpakId = package.name;
        if (package.source == Source::Repo && repoPercent < 0 && package.popularity > 0) repoPercent = package.popularity;
        if (package.source == Source::Aur && aurName.isEmpty()) aurName = package.name;
    }
    if (fixtureMode) { done({}); return; }
    auto result = std::make_shared<Ratings>();
    result->repoPercent = repoPercent;
    auto pending = std::make_shared<int>(0);
    auto finish = [result, pending, done] { if (--*pending == 0) done(*result); };
    ++*pending;  // Held until every request below is queued, so a cache hit cannot finish early
    if (!flatpakId.isEmpty() && QRegularExpression("^[A-Za-z0-9._-]+$").match(flatpakId).hasMatch()) {
        *pending += 2;
        cachedGet(QUrl("https://odrs.gnome.org/1.0/reviews/api/ratings/" + flatpakId), [result, finish](QByteArray data) { parseOdrs(data, result.get()); finish(); });
        cachedGet(QUrl("https://flathub.org/api/v2/stats/" + flatpakId), [result, finish](QByteArray data) { parseFlathubStats(data, result.get()); finish(); });
    }
    if (!aurName.isEmpty()) {
        ++*pending;
        aurInfo(aurName, [result, finish](AurInfo info, QString failure) {
            if (failure.isEmpty()) { result->aurVotes = info.votes; result->aurPopularity = info.popularity; }
            finish();
        });
    }
    finish();
}

QString chrootPacmanConf(const QString &hostConf, const std::function<QString(const QString &)> &readFile) {
    static const QStringList keptOptions{"Architecture", "SigLevel", "LocalFileSigLevel", "RemoteFileSigLevel"};
    constexpr int maxServers = 10;
    QString options = "[options]\n", repos, section, body;
    int servers = 0;
    auto flush = [&] {
        if (!section.isEmpty() && servers > 0) repos += "\n[" + section + "]\n" + body;
        body.clear(); servers = 0;
    };
    for (QString line : hostConf.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.startsWith('[') && line.endsWith(']')) { flush(); section = line.mid(1, line.size() - 2).trimmed(); continue; }
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (section == "options") { if (keptOptions.contains(key)) options += key + " = " + value + "\n"; continue; }
        if (section.isEmpty()) continue;
        if (key == "SigLevel" || key == "Usage") body += key + " = " + value + "\n";
        else if (key == "Server" && servers < maxServers) { body += "Server = " + value + "\n"; ++servers; }
        else if (key == "Include") {
            for (QString item : readFile(value).split('\n')) {
                item = item.trimmed();
                const int at = item.indexOf('=');
                if (at < 0 || item.startsWith('#') || item.left(at).trimmed() != "Server" || servers >= maxServers) continue;
                body += "Server = " + item.mid(at + 1).trimmed() + "\n"; ++servers;
            }
        }
    }
    flush();
    return options + repos;
}

QString aurBuildScript() {
    // makepkg.conf comes from devtools (the same file pkgctl uses). mkarchroot, arch-nspawn and makechrootpkg ask for root with sudo themselves.
    return "target=$1; flag=$2; root=$3; shift 3\n"
           "[ -n \"$root\" ] && [ -f \"$root/pacman.conf\" ] || { echo \"No build environment folder\"; exit 1; }\n"
           "if [ -f \"$root/root/.arch-chroot\" ]; then\n"
           "  echo \"==> Updating the build environment\"\n"
           "  arch-nspawn -C \"$root/pacman.conf\" \"$root/root\" pacman -Syu --noconfirm || exit\n"
           "else\n"
           "  echo \"==> Preparing the build environment (first time only)\"\n"
           "  [ -e \"$root/root\" ] && sudo rm -rf --one-file-system -- \"$root/root\"\n"
           "  mkarchroot -C \"$root/pacman.conf\" -M /usr/share/devtools/makepkg.conf.d/x86_64.conf \"$root/root\" base-devel || exit\n"
           "fi\n"
           "built=(); main=\n"
           "for item; do\n"
           "  name=${item%%$'\\t'*}; cd \"${item#*$'\\t'}\" || exit\n"
           "  extra=(); for p in \"${built[@]}\"; do extra+=(-I \"$p\"); done\n"
           "  makechrootpkg -r \"$root\" \"${extra[@]}\" || exit\n"
           "  found=; for f in ./*.pkg.tar.*; do case \"$f\" in *.sig) continue;; esac; [ \"$(pacman -Qpq \"$f\")\" = \"$name\" ] && found=$f; done\n"
           "  [ -n \"$found\" ] || { echo \"No built package named $name\"; exit 1; }\n"
           "  main=$(realpath \"$found\"); [ \"$name\" = \"$target\" ] || built+=(\"$main\")\n"
           "done\n"
           "echo \"==> Installing package\"\n"
           "if [ ${#built[@]} -gt 0 ]; then pkexec sh -c 'main=$1; shift; pacman -U --noconfirm --asdeps --needed \"$@\" && pacman -U --noconfirm $0 \"$main\"' \"$flag\" \"$main\" \"${built[@]}\"\n"
           "else pkexec pacman -U --noconfirm $flag \"$main\"; fi\n";
}

QString dependencyName(const QString &dependency) {
    const int at = dependency.indexOf(QRegularExpression("[<>=]"));
    return (at < 0 ? dependency : dependency.left(at)).trimmed();
}

QStringList srcinfoDependencies(const QString &srcinfo) {
    static const QRegularExpression dependsLine("^\\s*(?:depends|makedepends|checkdepends)(?:_x86_64)?\\s*=\\s*(\\S.*?)\\s*$");
    QStringList found, own;
    for (const QString &line : srcinfo.split('\n')) {
        const auto match = dependsLine.match(line);
        if (match.hasMatch()) { if (!found.contains(match.captured(1))) found << match.captured(1); }
        else if (line.trimmed().startsWith("pkgname =")) own << line.section('=', 1).trimmed();
    }
    QStringList result;
    for (const QString &dependency : found) if (!own.contains(dependencyName(dependency))) result << dependency;
    return result;
}

QStringList aurBuildOrder(const QString &root, const QMap<QString, QStringList> &needs) {
    QStringList order, visiting;
    std::function<void(const QString &)> visit = [&](const QString &name) {
        if (order.contains(name) || visiting.contains(name)) return;  // A cycle cannot be built; the build itself reports it
        visiting << name;
        for (const QString &need : needs.value(name)) if (needs.contains(need)) visit(need);
        order << name;
    };
    visit(root);
    return order;
}
