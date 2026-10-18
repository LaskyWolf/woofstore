#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "backend.h"
#include "catalog.h"
#include "merge.h"
#include "review.h"
#include "leftovers.h"
#include "ratings.h"

static void await(QEventLoop &loop, bool &done, int milliseconds = 3000) {
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [] { std::fputs("Check timed out\n", stderr); std::abort(); });
    timeout.start(milliseconds);
    if (!done) loop.exec();
    assert(done);
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    assert(validPackageName("visual-studio-code-bin"));
    assert(validPackageName("libfoo++"));
    for (const QString &name : {"--help", "../package", "foo;touch /tmp/x", "foo\nbar", "", "$(id)"}) assert(!validPackageName(name));
    assert(desktopQuote("/tmp/my app%20.AppImage") == "\"/tmp/my app%%20.AppImage\"");
    assert(desktopQuote("/tmp/a$b`c\"d\\e") == "\"/tmp/a\\\\$b\\\\`c\\\\\"d\\\\\\\\e\"");

    QTemporaryDir temp;
    assert(temp.isValid());
    const QString source = temp.path() + "/My App%25.AppImage";
    QFile file(source);
    assert(file.open(QIODevice::WriteOnly));
    QByteArray bytes(64, '\0');
    bytes.replace(0, 4, QByteArray("\x7f" "ELF", 4));
    bytes.replace(8, 3, QByteArray("AI\x02", 3));
    assert(file.write(bytes) == bytes.size());
    file.close();
    const QString root = temp.path() + "/user data";
    QString error;
    const QString imported = importAppImage(source, root, &error);
    assert(!imported.isEmpty() && error.isEmpty());
    assert(QFileInfo(imported).isExecutable() && QFileInfo::exists(source));
    QFile copy(imported);
    assert(copy.open(QIODevice::ReadOnly));
    assert(copy.readAll() == bytes);
    const auto apps = importedAppImages(root);
    assert(apps.size() == 1 && apps[0].name == "My App%25" && apps[0].path == imported);
    QFile launcher(root + "/applications/woof-store-" + apps[0].id + ".desktop");
    assert(launcher.open(QIODevice::ReadOnly));
    assert(launcher.readAll().contains("Exec=" + desktopQuote(imported).toUtf8()));
    assert(importAppImage(source, root, &error).isEmpty() && error.contains("already"));
    const QString invalid = temp.path() + "/fake.AppImage";
    QFile fake(invalid);
    assert(fake.open(QIODevice::WriteOnly));
    fake.write("#!/bin/sh\necho not an AppImage\n");
    fake.close();
    assert(importAppImage(invalid, temp.path() + "/invalid-data", &error).isEmpty());
    assert(!QFileInfo::exists(temp.path() + "/invalid-data"));

    Backend backend;
    assert(!backend.reviewedFilesUnchanged(&error));
    App invalidRecipe;
    invalidRecipe.base = "../anything";
    bool rejected = false;
    backend.prepareAur(invalidRecipe, [&](QString contents, QString commit, QString failure) {
        rejected = true;
        assert(contents.isEmpty() && commit.isEmpty() && !failure.isEmpty());
    });
    assert(rejected);
    int callbackCount = 0;
    for (bool timed : {false, true}) {
        QEventLoop loop;
        bool done = false;
        const int previous = callbackCount;
        backend.readCommand(timed ? "/usr/bin/sleep" : "/definitely-missing-woofstore-program", timed ? QStringList{"2"} : QStringList{},
            [&](int code, QString, QString failure) {
                ++callbackCount;
                assert(code != 0 && !failure.isEmpty());
                if (timed) assert(failure.contains("timed out"));
                done = true;
                loop.quit();
            }, timed ? 50 : 3000);
        await(loop, done);
        assert(callbackCount == previous + 1);
    }

    if (qEnvironmentVariableIsSet("STORE_LIVE_TESTS")) {
        QEventLoop loop;
        bool done = false;
        Apps matches;
        backend.searchAur("pacseek", [&](Apps results, QString failure) {
            assert(failure.isEmpty()); matches = results; done = true; loop.quit();
        });
        await(loop, done, 30000);
        App target;
        for (const auto &app : matches) if (app.id == "pacseek") target = app;
        assert(!target.id.isEmpty());
        done = false;
        backend.prepareAur(target, [&](QString contents, QString commit, QString failure) {
            assert(failure.isEmpty() && contents.contains("PKGBUILD") && commit.size() == 40);
            done = true; loop.quit();
        });
        await(loop, done, 70000);
        assert(backend.reviewedFilesUnchanged(&error));
        QFile recipe(backend.reviewPath() + "/PKGBUILD");
        assert(recipe.open(QIODevice::Append));
        recipe.write("\n# integration-test change\n");
        recipe.close();
        assert(!backend.reviewedFilesUnchanged(&error) && error.contains("changed"));
        std::puts("Live AUR search and changed-recipe check: PASS");
    }
    std::puts("Identifier, AppImage, review, and process checks: PASS");

    // pacman installs from the first repo in pacman.conf order; the catalogue must pick the same one.
    const auto repo = parseSyncPackages("extra\tkrita\t6.0-1\tPaint\ncachyos-extra-v3\tkrita\t6.0-1.1\tPaint\nextra\tbad;name\t1\tx\ncore\tbash\t5-1\tShell\thttps://www.gnu.org/software/bash/\n",
                                        {"cachyos-extra-v3", "core", "extra"});
    assert(repo.size() == 2);
    for (const auto &package : repo) if (package.name == "krita") assert(package.repo == "cachyos-extra-v3" && package.version == "6.0-1.1" && package.url.isEmpty());
    for (const auto &package : repo) if (package.name == "bash") assert(package.url == "https://www.gnu.org/software/bash/");
    assert(normalizeComponentId("org.KDE.krita.desktop") == "org.kde.krita");
    assert(isLibraryName("python-requests") && isLibraryName("ttf-dejavu") && !isLibraryName("pythonista") && !isLibraryName("krita"));

    // Tiny gzip of a three-package AUR dump: one voted, one with zero votes, one invalid name.
    const QByteArray dump("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x5d\x8d\x3d\x0b\xc2\x30\x10\x86\xff\xcb\xcd\x49\x3f\x14\x07\x33\x3b\x4a\x11\xc1\x2e\xe2\x10\xcd\x49\x03\x4d\x13\x93\x0b\x55\xa4\xff\xdd\x03\x41\x6c\xb7\xf7\xeb\xee\x39\xbf\xa1\xd1\x0e\x41\xc1\xe8\xfd\x1d\x04\xb4\x18\x93\xf5\x03\x07\xb5\xac\xd9\x37\xd9\xb5\x9e\x30\x81\x5a\x0b\xd8\x61\xba\x45\x1b\xe8\x3b\x30\x5c\x9f\x8e\x7b\x56\x1d\x51\x48\xaa\x2c\xc7\x02\x9f\xda\x85\x1e\xb9\x39\xf8\x90\x7b\x1d\x2d\xbd\x40\x55\xc5\x66\x12\x3f\xd2\x23\x5b\xa4\x19\x6a\xb5\x40\x55\x4b\x54\x67\x8d\xc1\x01\xfe\x9e\x48\x79\xd5\x66\x76\xb4\x9d\x2e\x1f\xb6\x44\x99\xa9\xce\x00\x00\x00", 151);
    bool ok = false;
    const QByteArray json = gunzip(dump, &ok);
    assert(ok);
    gunzip("not gzip", &ok);
    assert(!ok);
    const auto aur = parseAurDump(json, &ok);
    assert(ok && aur.size() == 2);
    assert(aur[0].name == "woof" && aur[0].detailed() && aur[0].url == "https://w.example" && aur[0].popularity == 0.5);
    assert(aur[1].name == "quiet" && !aur[1].detailed() && aur[1].description.isEmpty() && aur[1].version == "2-1");
    const QString cache = temp.path() + "/aur.bin";
    assert(writeAurCache(cache, aur));
    const auto reloaded = readAurCache(cache, &ok);
    assert(ok && reloaded.size() == 2 && reloaded[0].url == aur[0].url && reloaded[1].votes == 0);
    QFile stale(cache);
    assert(stale.open(QIODevice::WriteOnly) && stale.write("old format") > 0);
    stale.close();
    readAurCache(cache, &ok);
    assert(!ok);
    std::puts("Catalogue parsing and AUR cache checks: PASS");

    assert(stripVariantSuffix("krita-git") == "krita" && stripVariantSuffix("android-studio") == "android-studio");
    assert(projectKey("https://www.GitHub.com/Owner/Repo.git/issues") == "github.com/owner/repo");
    assert(projectKey("https://www.krita.org/en/") == "krita.org" && projectKey("not a url").isEmpty());
    assert(identityTokens("https://appflowy.io").intersects(identityTokens("https://appflowy.com")));
    assert(identityTokens("https://shmmodbus.github.io").intersects(identityTokens("https://github.com/shmmodbus/shm_modbus")));
    assert(!identityTokens("https://github.com/block/buzz").intersects(identityTokens("https://github.com/chidiwilliams/buzz")));

    auto appStream = [](const QString &id, const QString &name, const QString &homepage, Package package) {
        CatalogApp app; app.key = id; app.name = name; app.homepage = homepage; app.packages = {package}; app.origin = MetadataOrigin::AppStream;
        return app;
    };
    auto aurApp = [](const QString &name, const QString &homepage) {
        CatalogApp app; app.key = "aur:" + name; app.name = name; app.homepage = homepage; app.packages = {{Source::Aur, name, "aur", "1"}};
        return app;
    };
    MergeStats stats;
    const auto mergedCatalog = mergeCatalog({
        appStream("org.kde.krita", "Krita", "https://krita.org", {Source::Flatpak, "org.kde.krita", "flathub", "6"}),
        appStream("org.kde.krita", "Krita", "https://krita.org", {Source::Repo, "krita", "extra", "6"}),
        aurApp("krita-git", "https://krita.org"),                                    // rule 2: repo package name
        appStream("io.notes.one", "Notes", "https://notes-one.example", {Source::Flatpak, "io.notes.one", "flathub", "1"}),
        aurApp("notes", "https://github.com/someone-else/notes"),                     // same name, other project: stays apart
        appStream("com.example.Buzz", "Buzz", "https://github.com/chidiwilliams/buzz", {Source::Flatpak, "com.example.Buzz", "flathub", "1"}),
        aurApp("buzz-bin", "https://chidiwilliams.github.io/buzz"),                   // rule 3: same name and publisher
        aurApp("buzz-appimage", "https://github.com/block/buzz"),                     // same name, other publisher: stays apart
    }, {}, &stats);
    assert(mergedCatalog.size() == 5);  // Krita, Notes, Buzz, plus the unrelated AUR "notes" and "buzz-appimage"
    const CatalogApp &krita = mergedCatalog[0];
    assert(krita.packages.size() == 3 && krita.packages[0].source == Source::Repo && krita.packages[2].name == "krita-git");
    assert(stats.byId == 1 && stats.byName == 1 && stats.byHomepage == 1 && stats.nearMisses.size() == 2 && stats.multiSource == 2);
    MergeOverrides keepApart;
    keepApart.separate.insert("krita-git");
    assert(mergeCatalog({appStream("org.kde.krita", "Krita", "https://krita.org", {Source::Repo, "krita", "extra", "6"}), aurApp("krita-git", "https://krita.org")}, keepApart, nullptr).size() == 2);
    const auto twoIds = mergeCatalog({
        appStream("md.obsidian.obsidian", "Obsidian", "https://obsidian.md", {Source::Flatpak, "md.obsidian.Obsidian", "flathub", "1"}),
        appStream("obsidian", "Obsidian", "https://obsidian.md/", {Source::Repo, "obsidian", "extra", "1"}),
        appStream("notes", "Notes", "https://a.example", {Source::Repo, "notes", "extra", "1"}),
        appStream("org.b.notes", "Notes", "https://b.example", {Source::Flatpak, "org.b.Notes", "flathub", "1"})}, {}, nullptr);
    assert(twoIds.size() == 3 && twoIds[0].packages.size() == 2 && twoIds[0].packages[0].source == Source::Repo);  // other projects stay apart
    std::puts("Merge rule checks: PASS");

    const Rankings ranked = parseRankings(R"({"hits":[{"app_id":"org.kde.krita","installs_last_month":1000}]})",
                                          R"({"hits":[{"app_id":"org.gimp.GIMP"}]})", R"({"packagePopularities":[{"name":"firefox","popularity":60}]})");
    assert(ranked.popular == QStringList{"org.kde.krita"} && ranked.trending == QStringList{"org.gimp.GIMP"} && ranked.archPopularity.value("firefox") == 60);
    assert(parseRankings("offline", "", "").popular.isEmpty());
    assert(categoryGroup({"Graphics", "Game"}) == "Games" && categoryGroup({"Settings"}) == "System" && categoryGroup({"X-Unknown"}).isEmpty());
    CatalogApp firefox = appStream("org.mozilla.firefox", "Firefox", "", {Source::Repo, "firefox", "extra", "1"});
    firefox.summary = "Web browser"; firefox.keywords = {"internet"};
    CatalogApp fox = aurApp("foxit", ""); fox.summary = "PDF reader";
    CatalogApp browser = aurApp("lynx", ""); browser.summary = "Text web browser";
    const QList<CatalogApp> searchable{browser, fox, firefox};
    const QList<double> searchScores{appScore(browser, ranked), appScore(fox, ranked), appScore(firefox, ranked)};
    assert(searchScores[2] > searchScores[0]);  // 60 % of Arch users beats an unvoted AUR package
    assert(rankSearch(searchable, "fox", searchScores) == QList<int>({1, 2}));     // name prefix before name contains
    assert(rankSearch(searchable, "rowser", searchScores).isEmpty());             // summaries match at word starts only
    assert(rankSearch(searchable, "browser", searchScores) == QList<int>({2, 0}));  // summary matches, more popular first
    assert(rankSearch(searchable, "internet", searchScores) == QList<int>{2} && rankSearch(searchable, "  ", searchScores).isEmpty());
    std::puts("Ranking and search checks: PASS");

    const auto info = parseAurInfo(R"({"resultcount":1,"results":[{"Name":"foo-bin","PackageBase":"foo","Maintainer":null,"NumVotes":3,"Popularity":0.5,"FirstSubmitted":1700000000,"LastModified":1790000000,"OutOfDate":null,"URL":"https://foo.example"}]})", &ok);
    assert(ok && info.base == "foo" && info.maintainer.isEmpty() && info.votes == 3 && info.firstSubmitted.isValid() && !info.outOfDate.isValid());
    parseAurInfo(R"({"resultcount":0,"results":[]})", &ok);
    assert(!ok);
    const QString recipe = "\n──── PKGBUILD ────\n# curl is fine in a comment\nsource=(\"https://github.com/other/foo/archive/v$pkgver.tar.gz\" \"https://paste.example/x.sh\")\nsha256sums=('SKIP' 'SKIP')\nbuild() {\n  curl -s https://x.example/a | sh\n  npm install\n}\n"
                           "\n──── foo.install ────\npost_install() { echo $(echo aGk= | base64 -d); }\n\n──── .SRCINFO ────\npkgbase = foo\n";
    const QStringList flags = reviewFlags(recipe, "https://github.com/real/foo");
    auto flagged = [&flags](const QString &text) { for (const auto &f : flags) if (f.contains(text)) return true; return false; };
    assert(flagged("foo.install is an install script") && flagged("PKGBUILD:5 downloads") && flagged("pipes data into a shell") && flagged("language package manager"));
    assert(flagged("foo.install:1 decodes") && flagged("a different account") && flagged("paste.example") && flagged("2 checksum(s) are SKIP"));
    assert(!flagged("PKGBUILD:1"));  // comments are ignored
    assert(reviewFlags("\n──── PKGBUILD ────\nsource=(\"git+https://github.com/real/foo.git\")\nsha256sums=('SKIP')\n", "https://github.com/real/foo").isEmpty());
    QTemporaryDir reviewedDir;
    assert(saveReviewed(reviewedDir.path(), "foo", "abc123", recipe) && loadReviewed(reviewedDir.path(), "foo", nullptr) == recipe);
    QString savedCommit; loadReviewed(reviewedDir.path(), "foo", &savedCommit);
    assert(savedCommit == "abc123" && loadReviewed(reviewedDir.path(), "missing").isEmpty() && !saveReviewed(reviewedDir.path(), "../x", "c", "r"));
    std::puts("AUR review checks: PASS");

    QTemporaryDir fakeHome;
    auto touch = [&fakeHome](const QString &relative) {
        QDir().mkpath(QFileInfo(fakeHome.path() + '/' + relative).absolutePath());
        QFile file(fakeHome.path() + '/' + relative); assert(file.open(QIODevice::WriteOnly)); file.write("12345");
    };
    touch(".config/Krita/a.conf"); touch(".config/kritarc"); touch(".cache/krita/x"); touch(".local/share/krita/y"); touch(".local/share/applications/krita.desktop");
    touch(".config/other/z"); touch(".var/app/org.kde.krita/data/d"); touch(".config/icons/i");
    const auto left = findLeftovers(fakeHome.path(), {"krita", "icons", "go"});
    QStringList kinds;
    for (const auto &item : left) kinds << item.kind + ':' + QFileInfo(item.path).fileName() + (item.checkedByDefault ? "*" : "");
    kinds.sort();
    assert(kinds == QStringList({"Cache:krita*", "Data:krita", "Settings:Krita", "Settings:kritarc"}));  // not applications/, icons/, other/
    assert(left.first().bytes > 0 && flatpakData(fakeHome.path(), "org.kde.krita").size() == 1 && flatpakData(fakeHome.path(), "org.none").isEmpty() && flatpakData(fakeHome.path(), "../x").isEmpty());
    assert(!deleteLeftover(fakeHome.path(), "/etc/hostname") && !deleteLeftover(fakeHome.path(), fakeHome.path() + "/../x"));
    QFile::link(fakeHome.path() + "/.config/other", fakeHome.path() + "/.config/linked");
    assert(deleteLeftover(fakeHome.path(), fakeHome.path() + "/.config/linked") && QFileInfo::exists(fakeHome.path() + "/.config/other/z"));  // link only
    assert(deleteLeftover(fakeHome.path(), fakeHome.path() + "/.cache/krita") && !QFileInfo::exists(fakeHome.path() + "/.cache/krita"));
    assert(deleteLeftover(fakeHome.path(), fakeHome.path() + "/.var/app/org.kde.krita") && !QFileInfo::exists(fakeHome.path() + "/.var/app/org.kde.krita"));
    std::puts("Leftover checks: PASS");

    Ratings rated;
    parseOdrs(R"({"star0":0,"star1":0,"star2":0,"star3":0,"star4":5,"star5":5,"total":10})", &rated);
    assert(rated.reviews == 10 && qAbs(rated.stars - 4.5) < 0.001);
    Ratings few; parseOdrs(R"({"star5":2,"total":2})", &few); assert(few.reviews == 0 && !few.any());  // Too few reviews: no stars
    parseFlathubStats(R"({"installs_total":1781622,"installs_last_month":35610,"installs_per_day":{"2026-07-02":5,"2026-07-01":3}})", &few); assert(few.installs == 1781622 && few.any() && few.daily.size() == 2 && few.daily[0].first == "2026-07-01");
    const Rankings recent = parseRankings({}, {}, {}, R"({"hits":[{"app_id":"a.b.New"}]})", R"({"hits":[{"app_id":"a.b.Fresh"},{"app_id":"c.d.Fresh"}]})");
    assert(recent.added == QStringList{"a.b.New"} && recent.updated.size() == 2);
    std::puts("Ratings checks: PASS");

    const auto mpvEntry = parseLauncher("/usr/share/applications/mpv.desktop", "[Desktop Entry]\nType=Application\nName=mpv\nName[it]=Lettore mpv\nComment=Video player\nExec=mpv %U\nIcon=mpv\n[Desktop Action x]\nName=Other\n", "it_IT");
    assert(mpvEntry && mpvEntry->name == "Lettore mpv" && mpvEntry->comment == "Video player" && mpvEntry->id == "mpv" && !mpvEntry->flatpak);
    assert(parseLauncher("/x/org.a.B.desktop", "[Desktop Entry]\nType=Application\nName=B\nExec=b", "en_US")->flatpak == false);
    assert(parseLauncher("/v/flatpak/exports/share/applications/org.a.B.desktop", "[Desktop Entry]\nType=Application\nName=B\nExec=b", "en_US")->flatpak);
    assert(!parseLauncher("/x/h.desktop", "[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true", "en_US"));          // hidden
    assert(!parseLauncher("/x/l.desktop", "[Desktop Entry]\nType=Link\nName=L\nExec=l", "en_US"));                                 // not an application
    assert(!parseLauncher("/x/g.desktop", "[Desktop Entry]\nType=Application\nName=G\nOnlyShowIn=GNOME;", "en_US"));               // no Exec, other desktop
    assert(!parseLauncher("/x/s.desktop", "[Desktop Entry]\nType=Application\nName=Game\nExec=steam steam://rungameid/1", "en_US")); // Steam game
    const auto updates = parseArchUpdates("systemd  262-1 -> 262.1-1\nnoise line\nlinux  7.2.9-2  ->  7.2.11-1\n", "Arch");
    assert(updates.size() == 2 && updates[0].name == "systemd" && updates[1].to == "7.2.11-1" && updates[1].source == "Arch");
    const auto flatpakUpdates = parseFlatpakUpdates("org.a.B\tB\t2.0\t\norg.a.B\tB\t2.0\t\norg.c.D\tD\t1.1\t\n", {{"org.a.B", "1.0"}});
    assert(flatpakUpdates.size() == 2 && flatpakUpdates[0].from == "1.0" && flatpakUpdates[1].from.isEmpty());
    auto plain = [](const QString &html) { QString text = html; return text.remove(QRegularExpression("<[^>]*>")); };
    const auto kernel = versionDiff("7.2.9-2", "7.2.11-1");
    assert(plain(kernel.first) == "7.2.9-2" && plain(kernel.second) == "7.2.11-1" && kernel.first.startsWith("7.2.<span") && kernel.second.contains(">11-1<"));
    assert(versionDiff("5.2.9", "5.3.0").second == "5.<span style=\"color:#27ae60; font-weight:600\">3.0</span>");
    const auto longer = versionDiff("1.10", "1.1");  // Whole numbers change, never half a number
    assert(longer.first.contains(">10<") && longer.second.contains(">1<"));
    assert(versionDiff("262.1", "262.1.1").first == "262.1" && versionDiff("262.1", "262.1.1").second.contains(">.1<"));
    assert(plain(versionDiff("2.5.2-1.1", "2.5.3-1.1").second) == "2.5.3-1.1" && versionDiff("2.5.2-1.1", "2.5.3-1.1").second.contains(">3-1.1<"));
    assert(versionDiff("2.0", "2.0").first == "2.0");
    // AppImage update information
    QByteArray elf(64, '\0'); elf.replace(0, 4, QByteArray("\x7f" "ELF", 4)); elf[4] = 2; elf[5] = 1;
    const QByteArray updInfo = QByteArray("gh-releases-zsync|owner|repo|latest|Foo-*-x86_64.AppImage.zsync") + QByteArray(1, '\0') + QByteArray(20, '\xff');
    const QByteArray names = QByteArray("\0.shstrtab\0.upd_info\0", 21);
    auto put = [](QByteArray &bytes, int at, quint64 value, int size) { for (int i = 0; i < size; ++i) bytes[at + i] = char((value >> (8 * i)) & 0xff); };
    const int infoAt = 64, namesAt = infoAt + int(updInfo.size()), tableAt = namesAt + int(names.size());
    QByteArray table(64 * 3, '\0');  // null section, .shstrtab, .upd_info
    put(table, 64 + 0, 1, 4); put(table, 64 + 0x18, namesAt, 8); put(table, 64 + 0x20, names.size(), 8);
    put(table, 128 + 0, 11, 4); put(table, 128 + 0x18, infoAt, 8); put(table, 128 + 0x20, updInfo.size(), 8);
    put(elf, 0x28, tableAt, 8); put(elf, 0x3A, 64, 2); put(elf, 0x3C, 3, 2); put(elf, 0x3E, 1, 2);
    QTemporaryDir scratch;
    const QString imagePath = scratch.path() + "/Foo.AppImage";
    { QFile image(imagePath); assert(image.open(QIODevice::WriteOnly)); image.write(elf + updInfo + names + table); }
    assert(appImageUpdateInfo(imagePath) == "gh-releases-zsync|owner|repo|latest|Foo-*-x86_64.AppImage.zsync");
    assert(appImageUpdateInfo("/nonexistent").isEmpty());
    const auto updateInfo = parseUpdateInfo(appImageUpdateInfo(imagePath));
    assert(updateInfo && updateInfo->owner == "owner" && updateInfo->tag == "latest" && !parseUpdateInfo("gh-releases-direct|a|b|latest|x"));
    assert(parseUpdateInfo("zsync|https://example.org/Foo.AppImage.zsync")->zsyncUrl.endsWith(".zsync") && !parseUpdateInfo("zsync|file:///etc/passwd"));
    assert(zsyncSha1("zsync: 0.6.2\nFilename: Foo\nSHA-1: 0123456789ABCDEF0123456789abcdef01234567\n\n") == "0123456789abcdef0123456789abcdef01234567" && zsyncSha1("nothing").isEmpty());
    const auto asset = pickReleaseAsset(R"({"tag_name":"v2.0","assets":[{"name":"Foo-2.0-x86_64.AppImage","browser_download_url":"https://x/Foo-2.0-x86_64.AppImage"},{"name":"Foo-2.0-x86_64.AppImage.zsync","browser_download_url":"https://x/Foo-2.0-x86_64.AppImage.zsync"},{"name":"Other.AppImage","browser_download_url":"https://x/o"}]})", updateInfo->pattern);
    assert(asset.url == "https://x/Foo-2.0-x86_64.AppImage" && asset.zsyncUrl.endsWith(".zsync") && asset.version == "v2.0");
    assert(pickReleaseAsset("{}", "*").url.isEmpty());
    // News
    const auto feed = parseFeed("<rss><channel><item><title>Manual intervention needed</title><link>https://archlinux.org/news/x/</link><pubDate>Mon, 13 Oct 2025 12:00:00 +0000</pubDate><description>&lt;p&gt;Do this &lt;b&gt;first&lt;/b&gt;.&lt;/p&gt;</description></item><item><title>No link</title></item></channel></rss>", "Arch Linux", true);
    assert(feed.size() == 1 && feed[0].summary == "Do this first." && feed[0].important && feed[0].date.date() == QDate(2025, 10, 13) && feed[0].source == "Arch Linux");
    const auto blog = parseCachyBlog("<li><article><time datetime=\"2026-08-09\">Aug 9, 2026</time> · <a href=\"/category/release\">release</a><h2 class=\"x\"><a class=\"y\" href=\"/blog/2608-august-release\">CachyOS August 2026 Release</a></h2><p class=\"z\">Shelly Rewrite</p></article></li>");
    assert(blog.size() == 1 && blog[0].url == "https://cachyos.org/blog/2608-august-release" && blog[0].date.date() == QDate(2026, 8, 9) && blog[0].summary == "Shelly Rewrite");
    // The update script, run with stand-ins for pacman, paru, flatpak, curl and this program
    {
        QTemporaryDir bin; const QString log = bin.path() + "/log";
        auto stub = [&](const QString &name, const QString &body) { QFile f(bin.path() + "/" + name); assert(f.open(QIODevice::WriteOnly)); f.write("#!/bin/sh\n" + body.toUtf8()); f.close(); f.setPermissions(QFile::permissions(f.fileName()) | QFile::ExeOwner); };
        auto run = [&](const QString &script, const QStringList &args) {
            QFile::remove(log);
            QProcess p; auto env = QProcessEnvironment::systemEnvironment(); env.insert("PATH", bin.path() + ":" + env.value("PATH")); env.insert("LOG", log); p.setProcessEnvironment(env);
            p.start("bash", QStringList{"-c", script, "x", bin.path() + "/woof"} + args); p.waitForFinished(20000);
            QFile f(log); const QString calls = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            return QPair<int, QString>(p.exitCode(), calls);
        };
        stub("pkexec", "echo \"pkexec $*\" >> \"$LOG\"; exit ${FAIL_PACMAN:-0}\n");
        stub("paru", "echo \"paru $*\" >> \"$LOG\"; exit ${FAIL_PARU:-0}\n");
        stub("flatpak", "echo \"flatpak $*\" >> \"$LOG\"\n");
        stub("curl", "echo \"curl $*\" >> \"$LOG\"; for last; do :; done; : > \"$3\"\n");
        stub("woof", "echo \"woof $*\" >> \"$LOG\"\n");
        QStringList names;
        const QString full = updateScript(true, true, true, true, true, &names);
        assert(names == QStringList({"System packages", "AUR packages", "Flatpak apps", "AppImages", "Orphan packages"}));
        const QString image = "abc\thttps://x/Foo.AppImage\tFoo/Bar";
        auto ok = run(full, {image});
        assert(ok.first == 0 && ok.second.indexOf("pkexec pacman -Syu --noconfirm") < ok.second.indexOf("paru -Sua --noconfirm --skipreview --devel")
               && ok.second.indexOf("paru") < ok.second.indexOf("flatpak update -y --noninteractive") && ok.second.contains("curl -fsSL -o") && ok.second.contains("woof --import-appimage") && ok.second.contains("--replaces abc") && ok.second.contains("Foo_Bar.AppImage"));
        qputenv("FAIL_PACMAN", "1");
        auto stopped = run(full, {image});
        assert(stopped.first == 1 && stopped.second == "pkexec pacman -Syu --noconfirm\n");  // Nothing after a failed system upgrade
        qunsetenv("FAIL_PACMAN"); qputenv("FAIL_PARU", "1");
        auto partial = run(full, {image});
        assert(partial.first == 1 && partial.second.contains("flatpak update") && partial.second.contains("woof --import-appimage"));  // The rest still runs, the result reports the failure
        qunsetenv("FAIL_PARU");
        const QString onlyApps = updateScript(false, false, false, true, false, &names);
        assert(names == QStringList({"Flatpak apps"}) && !onlyApps.contains("pacman") && !onlyApps.contains("paru") && run(onlyApps, {}).second == "flatpak update -y --noninteractive\n");
    }
    {
        const auto orphans = parseOrphans("libfoo\t1.2 MiB\nbar-git\n\n$(rm -rf x)\t3 MiB\n");
        assert(orphans.size() == 2 && orphans[0].name == "libfoo" && orphans[0].size == "1.2 MiB" && orphans[1].name == "bar-git" && orphans[1].size.isEmpty());
    }
    std::puts("Installed and updates checks: PASS");
}
