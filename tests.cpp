#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include "backend.h"

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
    QFile launcher(root + "/applications/cachy-store-" + apps[0].id + ".desktop");
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
        backend.readCommand(timed ? "/usr/bin/sleep" : "/definitely-missing-cachystore-program", timed ? QStringList{"2"} : QStringList{},
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
}
