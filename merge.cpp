#include "merge.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

QString stripVariantSuffix(const QString &name) {
    static const QRegularExpression suffix("-(git|bin|appimage|nightly|beta|dev)$");
    QString base = name;
    base.remove(suffix);
    return base;
}

QString nameKey(const QString &text) {
    QString key;
    for (const QChar c : text.toLower()) if (c.isLetterOrNumber()) key += c;
    return key;
}

QString projectKey(const QString &homepage) {
    const QUrl url(homepage.trimmed());
    QString host = url.host().toLower();
    if (host.startsWith("www.")) host.remove(0, 4);
    if (host.isEmpty()) return {};
    // On shared code hosts the host alone says nothing; the owner/repo path identifies the project.
    static const QSet<QString> forges{"github.com", "gitlab.com", "codeberg.org", "sourceforge.net", "gitlab.gnome.org",
                                      "invent.kde.org", "bitbucket.org", "git.sr.ht", "gitlab.freedesktop.org"};
    if (!forges.contains(host)) return host;
    const QStringList parts = url.path().toLower().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) return {};
    QString repo = parts[1];
    if (repo.endsWith(".git")) repo.chop(4);
    return host + '/' + parts[0] + '/' + repo;
}

MergeOverrides loadMergeOverrides(const QStringList &dirs) {
    MergeOverrides overrides;
    for (const QString &dir : dirs) {
        QFile file(QDir(dir).filePath("merge-overrides.json"));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject merge = root.value("merge").toObject();
        for (auto it = merge.begin(); it != merge.end(); ++it) overrides.merge.insert(it.key(), normalizeComponentId(it.value().toString()));
        for (const auto &name : root.value("separate").toArray()) overrides.separate.insert(name.toString());
    }
    return overrides;
}

// Short names that identify who publishes a project: the site name (appflowy.com and appflowy.io -> "appflowy"),
// the subdomain on hosting services (shmmodbus.github.io -> "shmmodbus"), and the owner on code forges.
QSet<QString> identityTokens(const QString &url) {
    QSet<QString> tokens{projectKey(url)};
    const QUrl parsed(url.trimmed());
    QStringList labels = parsed.host().toLower().split('.', Qt::SkipEmptyParts);
    if (labels.value(0) == "www") labels.removeFirst();
    if (labels.size() < 2) { tokens.remove(QString()); return tokens; }
    static const QSet<QString> hostingSuffixes{"github.io", "gitlab.io", "sourceforge.net", "sourceforge.io", "itch.io", "codeberg.page",
                                               "netlify.app", "vercel.app", "pages.dev", "readthedocs.io"};
    static const QSet<QString> twoPartEndings{"co.uk", "org.uk", "com.br", "com.au", "co.jp", "com.cn"};
    const QString ending = labels.mid(labels.size() - 2).join('.');
    const QString forge = projectKey(url).section('/', 0, 0);
    if (!forge.isEmpty() && projectKey(url).contains('/')) tokens.insert("name:" + projectKey(url).section('/', 1, 1));  // forge owner
    else if (hostingSuffixes.contains(ending) || twoPartEndings.contains(ending)) { if (labels.size() >= 3) tokens.insert("name:" + labels[labels.size() - 3]); }
    else tokens.insert("name:" + labels[labels.size() - 2]);
    tokens.remove(QString());
    return tokens;
}

static QSet<QString> identityTokens(const CatalogApp &app) {
    QSet<QString> tokens = identityTokens(app.homepage);
    // A bug tracker like github.com/owner/repo/issues still names the repository.
    for (const Link &link : app.links) if (link.type != "donation" && link.type != "translate") tokens += identityTokens(link.url);
    return tokens;
}

static int sourceRank(Source source) {
    switch (source) {
    case Source::Repo: return 0;
    case Source::Flatpak: return 1;
    case Source::Aur: return 2;
    }
    return 3;
}

// Keeps the richer metadata of the two and lists both entries' packages.
static void absorb(CatalogApp &target, const CatalogApp &other) {
    target.packages += other.packages;
    if (other.origin != MetadataOrigin::None && other.description.size() > target.description.size()) {
        target.description = other.description;
        if (!other.screenshots.isEmpty()) target.screenshots = other.screenshots;
    }
    if (target.summary.isEmpty()) target.summary = other.summary;
    if (target.iconPath.isEmpty()) target.iconPath = other.iconPath;
    if (target.homepage.isEmpty()) target.homepage = other.homepage;
    for (const Link &link : other.links) if (!target.links.contains(link)) target.links.append(link);
    for (const QString &category : other.categories) if (!target.categories.contains(category)) target.categories.append(category);
}

QList<CatalogApp> mergeCatalog(QList<CatalogApp> apps, const MergeOverrides &overrides, MergeStats *stats) {
    MergeStats local;
    MergeStats &count = stats ? *stats : local;
    count = {};

    // Rule 1: entries with metadata and the same AppStream ID are one app.
    QList<CatalogApp> merged, loose;
    QHash<QString, qsizetype> byKey;
    for (CatalogApp &app : apps) {
        if (app.origin == MetadataOrigin::None) { loose.append(std::move(app)); continue; }
        const auto found = byKey.constFind(app.key);
        if (found != byKey.constEnd()) { absorb(merged[*found], app); ++count.byId; continue; }
        byKey.insert(app.key, merged.size());
        merged.append(std::move(app));
    }

    // Rule 3 between two metadata entries: Arch names many apps after their desktop file (obsidian.desktop) where
    // Flathub uses reverse DNS (md.obsidian.Obsidian). Same name, same project and complementary sources make one app.
    auto provides = [](const CatalogApp &app, Source source) {
        for (const Package &package : app.packages) if (package.source == source) return true;
        return false;
    };
    QHash<QString, QList<qsizetype>> flatpakOnlyByName;
    for (qsizetype i = 0; i < merged.size(); ++i)
        if (provides(merged[i], Source::Flatpak) && !provides(merged[i], Source::Repo)) flatpakOnlyByName[nameKey(merged[i].name)].append(i);
    QSet<qsizetype> absorbed;
    for (qsizetype i = 0; i < merged.size(); ++i) {
        if (provides(merged[i], Source::Flatpak) || !provides(merged[i], Source::Repo)) continue;
        const QSet<QString> project = identityTokens(merged[i]);
        for (const qsizetype candidate : flatpakOnlyByName.value(nameKey(merged[i].name))) {
            if (absorbed.contains(candidate) || provides(merged[candidate], Source::Repo) || !identityTokens(merged[candidate]).intersects(project)) continue;
            count.merges.append("project: " + merged[i].key + " -> " + merged[candidate].name);
            absorb(merged[candidate], merged[i]); absorbed.insert(i); ++count.byHomepage;
            break;
        }
    }
    if (!absorbed.isEmpty()) {
        QList<CatalogApp> kept;
        for (qsizetype i = 0; i < merged.size(); ++i) if (!absorbed.contains(i)) kept.append(std::move(merged[i]));
        merged = std::move(kept);
        byKey.clear();
        for (qsizetype i = 0; i < merged.size(); ++i) byKey.insert(merged[i].key, i);
    }

    QHash<QString, qsizetype> byRepoPackage;
    QHash<QString, QList<qsizetype>> byFlatpakName, byDisplayName;
    // Flatpak ID endings that name a role, not an app: com.spotify.Client is not the "client" package.
    static const QSet<QString> genericEndings{"client", "app", "desktop", "browser", "editor", "player", "studio",
                                              "launcher", "manager", "viewer", "tool", "tools", "gui"};
    for (qsizetype i = 0; i < merged.size(); ++i) {
        for (const Package &package : merged[i].packages) {
            if (package.source == Source::Repo) byRepoPackage.insert(package.name, i);
            const QString ending = package.name.section('.', -1).toLower();
            if (package.source == Source::Flatpak && ending.size() >= 3 && !genericEndings.contains(ending)) byFlatpakName[ending].append(i);
        }
        byDisplayName[nameKey(merged[i].name)].append(i);
    }

    for (CatalogApp &app : loose) {
        const QString name = app.packages.value(0).name;
        qsizetype target = -1;
        int *rule = nullptr;
        if (overrides.separate.contains(name)) { merged.append(std::move(app)); continue; }
        if (overrides.merge.contains(name)) {
            target = byKey.value(overrides.merge.value(name), -1);
            rule = &count.byOverride;
        }
        // Rule 2: package-name rules (krita-git -> the app that ships the krita package).
        const QString base = stripVariantSuffix(name);
        if (target < 0 && base != name && byRepoPackage.contains(base)) { target = byRepoPackage.value(base); rule = &count.byName; }
        // Rule 3: same name (display name or Flatpak ID ending) AND the same project (homepage, source code or
        // bug tracker). Short names like "buzz" belong to many projects, so a name alone is not enough.
        QList<qsizetype> sameName = byDisplayName.value(nameKey(base));
        for (const qsizetype candidate : byFlatpakName.value(base)) if (!sameName.contains(candidate)) sameName.append(candidate);
        if (target < 0) {
            const QSet<QString> project = identityTokens(app.homepage);
            for (const qsizetype candidate : sameName)
                if (identityTokens(merged[candidate]).intersects(project)) { target = candidate; rule = &count.byHomepage; break; }
            if (target < 0 && !sameName.isEmpty() && app.packages.value(0).source == Source::Aur)
                count.nearMisses.append(name + " ~ " + merged[sameName.first()].name + " (" + projectKey(app.homepage) + " vs "
                                        + projectKey(merged[sameName.first()].homepage) + ")");
        }
        if (target >= 0) {
            count.merges.append(QString(rule == &count.byName ? "name" : rule == &count.byHomepage ? "project" : "override") + ": " + name + " -> " + merged[target].name);
            absorb(merged[target], app); ++*rule; continue;
        }
        merged.append(std::move(app));
    }

    for (CatalogApp &app : merged) {
        std::stable_sort(app.packages.begin(), app.packages.end(), [](const Package &a, const Package &b) { return sourceRank(a.source) < sourceRank(b.source); });
        QSet<int> sources;
        for (const Package &package : app.packages) sources.insert(int(package.source));
        if (sources.size() > 1) ++count.multiSource;
    }
    return merged;
}
