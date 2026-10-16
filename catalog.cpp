#include "catalog.h"
#include "backend.h"
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <zlib.h>

QString normalizeComponentId(QString id) {
    id = id.toLower();
    if (id.endsWith(".desktop")) id.chop(8);
    return id;
}

bool isLibraryName(const QString &name) {
    static const QRegularExpression prefix("^(python2?|perl|ruby|nodejs|r|haskell|lib32|lua\\d*|go|rust|php|ocaml|texlive|ttf|otf|fonts?|mingw-w64|android|java|vim|emacs|kodi-addon)-");
    return prefix.match(name).hasMatch();
}

QList<RepoPackage> parseSyncPackages(const QString &expacOutput, const QStringList &repoOrder) {
    QHash<QString, RepoPackage> best;
    for (const QString &line : expacOutput.split('\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.split('\t');
        if (f.size() < 4 || !validPackageName(f[1])) continue;
        const RepoPackage package{f[1], f[0], f[2], f[3], f.value(4)};
        const auto existing = best.constFind(package.name);
        // Repos missing from the order list rank last.
        auto rank = [&repoOrder](const QString &repo) { const int i = repoOrder.indexOf(repo); return i < 0 ? repoOrder.size() : i; };
        if (existing == best.constEnd() || rank(package.repo) < rank(existing->repo)) best.insert(package.name, package);
    }
    return best.values();
}

QByteArray gunzip(const QByteArray &data, bool *ok) {
    if (ok) *ok = false;
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};  // 16: expect a gzip header
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = uInt(data.size());
    QByteArray out;
    char buffer[1 << 16];
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof buffer;
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) break;
        out.append(buffer, int(sizeof buffer - stream.avail_out));
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) return {};
    if (ok) *ok = true;
    return out;
}

QList<AurEntry> parseAurDump(const QByteArray &json, bool *ok) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (ok) *ok = error.error == QJsonParseError::NoError && doc.isArray();
    QList<AurEntry> entries;
    const QJsonArray items = doc.array();
    entries.reserve(items.size());
    for (const auto &value : items) {
        const QJsonObject item = value.toObject();
        AurEntry entry;
        entry.name = item.value("Name").toString();
        if (!validPackageName(entry.name)) continue;
        entry.version = item.value("Version").toString();
        entry.votes = item.value("NumVotes").toInt();
        if (entry.detailed()) {
            entry.description = item.value("Description").toString();
            entry.url = item.value("URL").toString();
            entry.popularity = item.value("Popularity").toDouble();
        }
        entries.append(entry);
    }
    return entries;
}

static const QByteArray cacheMagic = "WOOFAUR";
static const qint32 cacheVersion = 1;

bool writeAurCache(const QString &path, const QList<AurEntry> &entries) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file);
    out << cacheMagic << cacheVersion << qint64(entries.size());
    for (const AurEntry &e : entries) out << e.name << e.version << e.description << e.url << qint32(e.votes) << e.popularity;
    return out.status() == QDataStream::Ok && file.commit();
}

QList<AurEntry> readAurCache(const QString &path, bool *ok) {
    if (ok) *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QDataStream in(&file);
    QByteArray magic; qint32 version = 0; qint64 count = 0;
    in >> magic >> version >> count;
    // A cache from another format version is rebuilt from the dump.
    if (magic != cacheMagic || version != cacheVersion || count < 0 || count > 10'000'000) return {};
    QList<AurEntry> entries;
    entries.reserve(count);
    for (qint64 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        AurEntry e; qint32 votes = 0;
        in >> e.name >> e.version >> e.description >> e.url >> votes >> e.popularity;
        e.votes = votes;
        entries.append(e);
    }
    if (in.status() != QDataStream::Ok) return {};
    if (ok) *ok = true;
    return entries;
}

Rankings parseRankings(const QByteArray &popular, const QByteArray &trending, const QByteArray &pkgstats) {
    Rankings rankings;
    for (const auto &value : QJsonDocument::fromJson(popular).object().value("hits").toArray()) {
        const auto hit = value.toObject();
        const QString id = hit.value("app_id").toString();
        if (id.isEmpty()) continue;
        rankings.popular.append(id);
        rankings.flathubInstalls.insert(id, hit.value("installs_last_month").toDouble());
    }
    for (const auto &value : QJsonDocument::fromJson(trending).object().value("hits").toArray()) {
        const QString id = value.toObject().value("app_id").toString();
        if (!id.isEmpty()) rankings.trending.append(id);
    }
    for (const auto &value : QJsonDocument::fromJson(pkgstats).object().value("packagePopularities").toArray()) {
        const auto package = value.toObject();
        rankings.archPopularity.insert(package.value("name").toString(), package.value("popularity").toDouble());
    }
    return rankings;
}

double appScore(const CatalogApp &app, const Rankings &rankings) {
    // ponytail: hand-tuned scales (1M Flathub installs = 100 % of Arch users = 1); replace with a learned blend if rankings look off.
    double score = 0;
    for (const auto &package : app.packages) {
        if (package.source == Source::Flatpak) score = qMax(score, std::log10(1 + rankings.flathubInstalls.value(package.name)) / 6);
        // pkgstats also counts packages pulled in as dependencies; their helper apps ship no screenshots.
        if (package.source == Source::Repo) score = qMax(score, rankings.archPopularity.value(package.name) / 100 * (app.screenshots.isEmpty() ? .3 : 1));
        if (package.source == Source::Aur) score = qMax(score, package.popularity / (package.popularity + 1) / 2);
    }
    return score + (app.origin == MetadataOrigin::None ? 0 : .05);
}

QString categoryGroup(const QStringList &categories) {
    static const QList<QPair<QString, QStringList>> groups{
        {"Games", {"Game"}}, {"Graphics & Photography", {"Graphics"}}, {"Audio & Video", {"AudioVideo", "Audio", "Video"}},
        {"Developer Tools", {"Development"}}, {"Education & Science", {"Education", "Science"}}, {"Networking", {"Network"}},
        {"Productivity", {"Office"}}, {"System", {"System", "Settings"}}, {"Utilities", {"Utility"}}};
    for (const auto &group : groups)
        for (const QString &id : group.second) if (categories.contains(id)) return group.first;
    return {};
}

// True when a word in text starts with needle: "spo" finds "Spotify client", not "transport".
static bool wordStartMatch(const QString &text, const QString &needle) {
    for (qsizetype at = text.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0; at = text.indexOf(needle, at + 1, Qt::CaseInsensitive))
        if (at == 0 || !text[at - 1].isLetterOrNumber()) return true;
    return false;
}

QList<int> rankSearch(const QList<CatalogApp> &apps, const QString &query, const QList<double> &scores) {
    const QString needle = query.trimmed();
    if (needle.isEmpty()) return {};
    QList<QPair<int, int>> hits;  // (tier, index)
    for (int i = 0; i < apps.size(); ++i) {
        const auto &app = apps[i];
        int tier = -1;
        if (app.name.compare(needle, Qt::CaseInsensitive) == 0) tier = 0;
        else if (app.name.startsWith(needle, Qt::CaseInsensitive)) tier = 1;
        else if (app.name.contains(needle, Qt::CaseInsensitive)) tier = 2;
        else {
            for (const auto &package : app.packages) if (package.name.contains(needle, Qt::CaseInsensitive)) { tier = 2; break; }
            if (tier < 0) for (const QString &keyword : app.keywords) if (wordStartMatch(keyword, needle)) { tier = 3; break; }
            if (tier < 0 && wordStartMatch(app.summary, needle)) tier = 4;
        }
        if (tier >= 0) hits.append({tier, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [&scores](const QPair<int, int> &a, const QPair<int, int> &b) {
        if (a.first != b.first) return a.first < b.first;
        return scores.value(a.second) > scores.value(b.second);
    });
    QList<int> result;
    result.reserve(hits.size());
    for (const auto &hit : hits) result.append(hit.second);
    return result;
}
