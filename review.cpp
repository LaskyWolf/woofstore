#include "review.h"
#include "backend.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>
#include <QUrl>

AurInfo parseAurInfo(const QByteArray &json, bool *ok) {
    AurInfo info;
    const auto results = QJsonDocument::fromJson(json).object().value("results").toArray();
    if (ok) *ok = !results.isEmpty();
    if (results.isEmpty()) return info;
    const auto r = results.first().toObject();
    auto time = [&r](const char *key) { const auto v = r.value(key); return v.isNull() || v.isUndefined() ? QDateTime() : QDateTime::fromSecsSinceEpoch(qint64(v.toDouble()), QTimeZone::utc()); };
    info.name = r.value("Name").toString();
    info.base = r.value("PackageBase").toString();
    info.maintainer = r.value("Maintainer").toString();
    info.url = r.value("URL").toString();
    info.firstSubmitted = time("FirstSubmitted");
    info.lastModified = time("LastModified");
    info.outOfDate = time("OutOfDate");
    info.votes = r.value("NumVotes").toInt();
    info.popularity = r.value("Popularity").toDouble();
    return info;
}

namespace {
struct RecipeFile { QString name, text; };

QList<RecipeFile> splitRecipe(const QString &recipe) {
    static const QRegularExpression header("\n──── (.+?) ────\n");
    QList<RecipeFile> files;
    auto it = header.globalMatch(recipe);
    qsizetype bodyStart = -1;
    QString name;
    while (it.hasNext()) {
        const auto match = it.next();
        if (bodyStart >= 0) files.append({name, recipe.mid(bodyStart, match.capturedStart() - bodyStart)});
        name = match.captured(1); bodyStart = match.capturedEnd();
    }
    if (bodyStart >= 0) files.append({name, recipe.mid(bodyStart)});
    return files;
}

QString shorten(QString line) {
    line = line.simplified();
    return line.size() > 110 ? line.left(107) + "…" : line;
}

QString hostOf(QString url) {
    url.replace(QRegularExpression("\\$\\{[^}]*\\}|\\$\\w+"), "x");  // pkgver and friends
    return QUrl(url).host().toLower().remove(QRegularExpression("^www\\."));
}

// "owner" of a forge URL, so github.com/a/x and github.com/b/x are different publishers.
QString forgeOwner(const QString &url) {
    const QString host = hostOf(url);
    if (host != "github.com" && host != "gitlab.com" && host != "codeberg.org") return {};
    QString path = url; path.replace(QRegularExpression("\\$\\{[^}]*\\}|\\$\\w+"), "x");
    return QUrl(path).path().section('/', 1, 1).toLower();
}

bool sameSite(const QString &a, const QString &b) { return a == b || a.endsWith('.' + b) || b.endsWith('.' + a); }
}  // namespace

QStringList reviewFlags(const QString &recipe, const QString &upstreamUrl) {
    QStringList flags;
    const auto files = splitRecipe(recipe);
    QStringList extra;
    for (const auto &file : files) {
        if (file.name.endsWith(".install")) flags << QString("%1 is an install script. pacman runs it as root when the package is installed.").arg(file.name);
        else if (file.name != "PKGBUILD" && file.name != ".SRCINFO" && file.name != ".gitignore" && !file.name.endsWith(".patch") && !file.name.endsWith(".diff")
                 && !file.name.startsWith("LICENSE") && !file.name.startsWith("README")) extra << file.name;
    }
    if (!extra.isEmpty()) flags << "Extra files besides PKGBUILD: " + extra.join(", ") + ". Read each one.";

    struct Rule { QRegularExpression pattern; QString message; };
    static const QList<Rule> rules = {
        {QRegularExpression("\\b(curl|wget)\\b"), "downloads something with curl or wget"},
        {QRegularExpression("\\|\\s*(sudo\\s+)?(ba|z|da)?sh\\b"), "pipes data into a shell"},
        {QRegularExpression("\\b(npm|npx|bun|bunx|yarn|pnpm|pip3?|pipx|gem install|cargo install|go install)\\b"), "runs a language package manager, which fetches code outside source="},
        {QRegularExpression("\\bbase64\\b|\\beval\\b|\\bxxd\\s+-r|openssl\\s+enc"), "decodes or evaluates hidden text"},
        {QRegularExpression("\\bsudo\\b|\\bsu\\s+-"), "uses sudo or su"},
        {QRegularExpression("chmod\\s+(\\S+\\s+)?([ug]\\+s|[2467][0-7]{3}\\b)"), "sets a setuid or setgid permission"}};
    for (const auto &file : files) {
        if (file.name != "PKGBUILD" && !file.name.endsWith(".install")) continue;
        const QStringList lines = file.text.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].trimmed().startsWith('#')) continue;
            for (const auto &rule : rules)
                if (rule.pattern.match(lines[i]).hasMatch()) flags << QString("%1:%2 %3: %4").arg(file.name).arg(i + 1).arg(rule.message, shorten(lines[i]));
        }
    }

    for (const auto &file : files) {
        if (file.name != "PKGBUILD") continue;
        static const QRegularExpression sourceArray("(?:^|\n)source(?:_\\w+)?=\\(([^)]*)\\)");
        static const QRegularExpression url("[a-z+]+://[^\\s'\")]+");
        QStringList hosts; int vcs = 0;
        auto sources = sourceArray.globalMatch(file.text);
        const QString upstreamHost = hostOf(upstreamUrl), upstreamOwner = forgeOwner(upstreamUrl);
        while (sources.hasNext()) {
            auto urls = url.globalMatch(sources.next().captured(1));
            while (urls.hasNext()) {
                const QString address = urls.next().captured();
                static const QRegularExpression vcsPrefix("^(git|svn|hg|bzr)\\+");
                if (vcsPrefix.match(address).hasMatch()) ++vcs;
                QString plain = address; plain.remove(vcsPrefix);
                const QString host = hostOf(plain);
                if (host.isEmpty() || hosts.contains(host)) continue;
                hosts << host;
                static const QRegularExpression raw("^\\d+\\.\\d+\\.\\d+\\.\\d+$");
                static const QRegularExpression dropSite("paste|transfer\\.sh|0x0\\.st|bit\\.ly|tinyurl|ngrok|anonfiles|file\\.io|temp\\.sh");
                if (raw.match(host).hasMatch() || dropSite.match(host).hasMatch()) flags << "A source comes from " + host + ", an address or file-sharing site, not a project host.";
                else if (!upstreamHost.isEmpty() && !sameSite(host, upstreamHost) && upstreamHost != "github.com" && upstreamHost != "gitlab.com")
                    flags << QString("A source comes from %1, but the project website is %2.").arg(host, upstreamHost);
                else if (!upstreamOwner.isEmpty() && host == upstreamHost && forgeOwner(address) != upstreamOwner)
                    flags << QString("A source comes from %1/%2, but the project website is %1/%3: a different account.").arg(host, forgeOwner(address), upstreamOwner);
            }
        }
        static const QRegularExpression sums("(?:^|\n)\\w*sums(?:_\\w+)?=\\(([^)]*)\\)");
        int skips = 0;
        auto sumArrays = sums.globalMatch(file.text);
        while (sumArrays.hasNext()) skips += sumArrays.next().captured(1).count("SKIP");
        if (skips > vcs) flags << QString("%1 checksum(s) are SKIP for files that are not a git checkout: those downloads are not verified.").arg(skips - vcs);
    }
    flags.removeDuplicates();
    return flags;
}

bool saveReviewed(const QString &dir, const QString &base, const QString &commit, const QString &recipe) {
    if (!validPackageName(base)) return false;
    QDir().mkpath(dir);
    QSaveFile file(dir + '/' + base + ".txt");
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(commit.toUtf8() + '\n' + recipe.toUtf8());
    return file.commit();
}

QString loadReviewed(const QString &dir, const QString &base, QString *commit) {
    if (!validPackageName(base)) return {};
    QFile file(dir + '/' + base + ".txt");
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString all = QString::fromUtf8(file.readAll());
    if (commit) *commit = all.section('\n', 0, 0);
    return all.section('\n', 1);
}
