// cxx-qt-lib has no QApplication, and the KDE style for Qt Quick Controls needs one, so the window starts here. One window per
// user: a second start (a KRunner suggestion, a link, the menu) hands its request to the running one over the session bus.
#include "shell.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QIcon>
#include <QMessageBox>
#include <QPointer>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QWheelEvent>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

static const char *service = "io.github.laskywolf.WoofStore";
static QPointer<QWindow> mainWindow;  // Message boxes stay on top of it when no other window has the focus

bool message_box(int kind, const QString &title, const QString &text) {
    QMessageBox box(kind == 1 ? QMessageBox::Warning : kind == 2 ? QMessageBox::Question : QMessageBox::Information, title, text,
                    kind == 2 ? QMessageBox::Yes | QMessageBox::No : QMessageBox::Ok);
    box.winId();  // Creates the native window, so it can be tied to the window that asked
    if (QWindow *parent = QGuiApplication::focusWindow() ? QGuiApplication::focusWindow() : mainWindow.data()) box.windowHandle()->setTransientParent(parent);
    return box.exec() == (kind == 2 ? QMessageBox::Yes : QMessageBox::Ok);
}

// The bus object other programs call: Open(link) shows the link in the window. A virtual object needs no moc.
class OpenService : public QDBusVirtualObject {
public:
    explicit OpenService(QObject *root) : root(root) {}
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        if (message.member() != "Open" || message.arguments().size() != 1) return false;
        QMetaObject::invokeMethod(root, "openRequest", Q_ARG(QVariant, message.arguments().first()));
        connection.send(message.createReply());
        return true;
    }
    QString introspect(const QString &path) const override {
        Q_UNUSED(path);
        return QString("<interface name=\"%1\"><method name=\"Open\"><arg name=\"link\" type=\"s\" direction=\"in\"/></method></interface>").arg(service);
    }
private:
    QObject *root;
};

// image://icon/NAME|FALLBACK|SCALE: a theme icon, as QIcon::fromTheme draws it for widgets (symbolic icons follow the palette).
class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override {
        const QStringList names = QUrl::fromPercentEncoding(id.toUtf8()).split('|');  // The URL arrives with | encoded
        QIcon icon = QIcon::fromTheme(names.value(0));
        if (icon.isNull()) icon = QIcon::fromTheme(names.value(1, "application-x-executable"));
        const qreal scale = qMax(1.0, names.value(2, "1").toDouble());
        const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(48, 48);
        // Ask for the logical size: the theme then draws the 16 px icon for a 16 px place, not the 22 px one
        const QPixmap pixmap = icon.pixmap((QSizeF(want) / scale).toSize(), scale);
        if (size) *size = pixmap.size();
        return pixmap;
    }
};

int run_shell(rust::Str url, rust::Str link) {
    static int argc = 1;
    static char name[] = "woof-store";
    static char *argv[] = {name, nullptr};
    QApplication app(argc, argv);
    app.setApplicationName("WoofStore");
    app.setOrganizationName("WoofStore");
    app.setDesktopFileName("io.github.laskywolf.WoofStore");  // Matches the installed launcher, so the task bar shows the right icon
    app.setWindowIcon(QIcon::fromTheme("io.github.laskywolf.WoofStore"));
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("breeze");
    QIcon::setFallbackThemeName("breeze");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle("org.kde.desktop");

    const QString request = QString::fromUtf8(link.data(), int(link.size()));
    const bool check = qEnvironmentVariableIsSet("WOOFSTORE_QUIT_AFTER_MS");  // Checks run beside the store the person has open
    auto bus = QDBusConnection::sessionBus();
    if (!check && bus.isConnected() && !bus.registerService(service)) {
        QDBusMessage call = QDBusMessage::createMethodCall(service, "/", service, "Open");
        call << request;
        if (bus.call(call, QDBus::Block, 5000).type() != QDBusMessage::ErrorMessage) return 0;
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider("icon", new IconProvider);
    engine.load(QUrl(QString::fromUtf8(url.data(), int(url.size()))));
    if (engine.rootObjects().isEmpty()) return 1;
    QObject *root = engine.rootObjects().first();
    mainWindow = qobject_cast<QWindow *>(root);
    // WOOFSTORE_PREVIEW: a page and states to show for a screenshot, e.g. "installed,expanded" (see Main.qml)
    if (qEnvironmentVariableIsSet("WOOFSTORE_PREVIEW")) root->setProperty("preview", qEnvironmentVariable("WOOFSTORE_PREVIEW"));
    OpenService open(root);
    if (!check && bus.isConnected()) bus.registerVirtualObject("/", &open);
    if (!request.isEmpty()) QMetaObject::invokeMethod(root, "openRequest", Q_ARG(QVariant, request));

    // For checks without a person: WOOFSTORE_SCRIPT plays input from a file, one step per line:
    // "wait MS", "move X Y", "click X Y", "wheel X Y DY" (DY in wheel steps of 120), "type TEXT", "key NAME", "shot PATH",
    // "eval JS" (prints the result).
    // The store quits after the last step.
    if (check && qEnvironmentVariableIsSet("WOOFSTORE_SCRIPT")) {
        QFile file(qEnvironmentVariable("WOOFSTORE_SCRIPT"));
        if (!file.open(QIODevice::ReadOnly)) { qWarning("WOOFSTORE_SCRIPT: cannot read the file"); return 1; }
        const QStringList steps = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        static QElapsedTimer clock; clock.start();  // Real timestamps: Qt Quick measures wheel and drag speed between events
        auto *step = new std::function<void(int)>;
        *step = [&app, root, steps, step](int i) {
            if (i >= steps.size()) { app.quit(); return; }
            const QString line = steps[i].trimmed();
            const QString verb = line.section(' ', 0, 0), rest = line.section(' ', 1);
            const QStringList a = rest.split(' ', Qt::SkipEmptyParts);
            auto *window = qobject_cast<QQuickWindow *>(root->property("shotTarget").value<QObject *>());
            if (!window) window = qobject_cast<QQuickWindow *>(root);
            int delay = 0;
            const QPointF at = a.size() >= 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF();
            const QPointF global = window->mapToGlobal(at);
            if (verb == "wait") delay = a.value(0).toInt();
            else if (verb == "move") { QMouseEvent e(QEvent::MouseMove, at, at, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier); e.setTimestamp(clock.elapsed()); QCoreApplication::sendEvent(window, &e); }
            else if (verb == "click") {
                QMouseEvent press(QEvent::MouseButtonPress, at, at, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); press.setTimestamp(clock.elapsed()); QCoreApplication::sendEvent(window, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, at, at, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier); release.setTimestamp(clock.elapsed()); QCoreApplication::sendEvent(window, &release);
            } else if (verb == "wheel") {
                QWheelEvent e(at, global, QPoint(), QPoint(0, -int(a.value(2).toDouble() * 120)), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                e.setTimestamp(clock.elapsed()); QCoreApplication::sendEvent(window, &e);
            } else if (verb == "type" || verb == "key") {  // "type TEXT" types characters; "key NAME" presses Return, Escape, Down, Up, Tab
                const QHash<QString, int> names = {{"Return", Qt::Key_Return}, {"Escape", Qt::Key_Escape}, {"Down", Qt::Key_Down}, {"Up", Qt::Key_Up}, {"Tab", Qt::Key_Tab}, {"Backspace", Qt::Key_Backspace}};
                const QList<QPair<int, QString>> keys = verb == "key" ? QList<QPair<int, QString>>{{names.value(rest), QString()}} : [&] {
                    QList<QPair<int, QString>> list;
                    for (const QChar c : rest) list.append({c.toUpper().unicode(), QString(c)});
                    return list;
                }();
                for (const auto &[code, text] : keys) {
                    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, text); QCoreApplication::sendEvent(window, &press);
                    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier, text); QCoreApplication::sendEvent(window, &release);
                }
            } else if (verb == "shot") window->grabWindow().save(rest);
            else if (verb == "eval") {
                QQmlExpression expression(qmlContext(root), root, rest);  // The root file's own context, so its ids work
                const QVariant value = expression.evaluate();
                fprintf(stderr, "eval %s => %s\n", qPrintable(rest), qPrintable(expression.hasError() ? expression.error().toString() : value.toString()));
            } else if (!line.startsWith('#')) qWarning().noquote() << "WOOFSTORE_SCRIPT: unknown step" << line;
            QTimer::singleShot(delay, &app, [step, i] { (*step)(i + 1); });
        };
        QTimer::singleShot(qEnvironmentVariableIntValue("WOOFSTORE_QUIT_AFTER_MS"), &app, [step] { (*step)(0); });
    } else if (check) {  // Save the window to WOOFSTORE_SCREENSHOT, then quit
        QTimer::singleShot(qEnvironmentVariableIntValue("WOOFSTORE_QUIT_AFTER_MS"), &app, [&app, root] {
            const QString shot = qEnvironmentVariable("WOOFSTORE_SCREENSHOT");
            // The root's shotTarget names another window to save, e.g. the Activity window
            auto *window = qobject_cast<QQuickWindow *>(root->property("shotTarget").value<QObject *>());
            if (!window) window = qobject_cast<QQuickWindow *>(root);
            if (window && !shot.isEmpty()) window->grabWindow().save(shot);
            app.quit();
        });
    }
    return app.exec();
}
