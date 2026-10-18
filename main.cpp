#include "backend.h"
#include "explore.h"
#include "appdetails.h"
#include <QtWidgets>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KParts/ReadOnlyPart>
#include <kde_terminal_interface.h>
#include <unistd.h>
#include "installflow.h"

static QLabel *label(const QString &text, int size = 0, bool bold = false) {
    auto *item = new QLabel(text);
    item->setTextFormat(Qt::PlainText);
    item->setWordWrap(true);
    QFont font = item->font();
    if (size) font.setPointSize(size);
    font.setBold(bold);
    item->setFont(font);
    return item;
}

static QPushButton *button(const QString &text, const QString &icon = {}) {
    auto *item = new QPushButton(QIcon::fromTheme(icon), text);
    item->setMinimumHeight(32);
    item->setCursor(Qt::PointingHandCursor);
    return item;
}

static QIcon appIcon(const App &app) {
    if (!app.icon.isNull()) return app.icon;
    return QIcon::fromTheme(app.id, QIcon::fromTheme("application-x-executable"));
}

// Quickpeek's Tray treatment: desktop palette, translucent fill, faint edge.
class Tray : public QFrame {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = palette().color(QPalette::Button);
        fill.setAlpha(150);
        QColor edge = palette().color(QPalette::Mid);
        edge.setAlpha(90);
        painter.setPen(QPen(edge, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 7, 7);
    }
};

class Store : public QMainWindow {
    Q_OBJECT
public:
    Backend backend;
    ~Store() override {
        if (part) { disconnect(part, nullptr, this, nullptr); delete part; }
    }
    explicit Store(bool loadCatalogue = true) : inventoryEnabled(loadCatalogue) {
        setWindowTitle("WoofStore");
        setWindowIcon(QIcon::fromTheme("system-software-install"));
        resize(1180, 850);
        setMinimumSize(720, 580);
        auto *root = new QWidget;
        root->setAutoFillBackground(true);
        auto *layout = new QHBoxLayout(root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        setCentralWidget(root);
        sidebar = new QWidget;
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(156);
        sidebar->setAutoFillBackground(false);
        auto palette = sidebar->palette();
        palette.setColor(QPalette::Base, palette.color(QPalette::Window));
        sidebar->setPalette(palette);
        auto *side = new QVBoxLayout(sidebar);
        side->setContentsMargins(8, 16, 8, 8);
        side->setSpacing(16);
        compactSidebar = new QAction(lucideIcon("panel-close"), "Compact sidebar", this);
        compactSidebar->setObjectName("compactSidebar");
        compactSidebar->setCheckable(true);
        auto *sidebarToggle = new QToolButton;
        sidebarToggle->setObjectName("sidebarToggle");
        sidebarToggle->setAutoRaise(true);
        sidebarToggle->setDefaultAction(compactSidebar);
        sidebarToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
        sidebarToggle->setFixedSize(40, 40); sidebarToggle->setIconSize(QSize(22, 22));
        side->addWidget(sidebarToggle, 0, Qt::AlignLeft);
        const QString navigationStyle = "QToolButton {border: none; border-radius: 8px; padding: 10px; background: transparent;} QToolButton:hover {background: palette(button);} QToolButton:checked {background: palette(midlight);} QToolButton:focus {border: 1px solid palette(highlight);}";
        sidebarToggle->setStyleSheet(navigationStyle);
        navigation = new QButtonGroup(this); navigation->setObjectName("pageNavigation"); navigation->setExclusive(true);
        auto *navHost = new QWidget; navHost->setObjectName("navigation");
        auto *navLayout = new QVBoxLayout(navHost); navLayout->setContentsMargins(0, 0, 0, 0); navLayout->setSpacing(8);
        int id = 0;
        for (const auto &entry : QList<QPair<QString, QString>>{{"Explore", "compass"}, {"Installed", "list"}, {"Updates", "refresh"}}) {
            auto *item = new NavButton(entry.first, entry.second);
            item->setObjectName("nav" + entry.first);
            item->setAccessibleName(entry.first); item->setToolTip(entry.first);
            navigation->addButton(item, id++); navLayout->addWidget(item);
        }
        navigation->button(0)->setChecked(true);
        side->addWidget(navHost); side->addStretch();
        activityButton = new ActivityButton; activityButton->setObjectName("activityButton"); side->addWidget(activityButton);
        connect(activityButton, &QAbstractButton::clicked, this, [this] { showActivity(); });
        sidebarMotion.setParent(this); sidebarMotion.setObjectName("sidebarMotion"); sidebarMotion.setEasingCurve(QEasingCurve::OutCubic);
        connect(&sidebarMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { sidebar->setFixedWidth(value.toInt()); });
        connect(compactSidebar, &QAction::toggled, this, [this, sidebarToggle](bool compact) {
            sidebarMotion.stop(); const int target = compact ? 56 : 156;
            const int duration = isVisible() ? desktopAnimationDuration(240) : 0;
            if (duration) { sidebarMotion.setDuration(duration); sidebarMotion.setStartValue(sidebar->width()); sidebarMotion.setEndValue(target); sidebarMotion.start(); }
            else sidebar->setFixedWidth(target);
            sidebarDivider->setVisible(!compact);
            activityButton->compact = compact;
            activityButton->update();
            compactSidebar->setIcon(lucideIcon(compact ? "panel-open" : "panel-close"));
            sidebarToggle->setToolTip(compact ? "Expand sidebar" : "Collapse sidebar");
            sidebarToggle->setAccessibleName(sidebarToggle->toolTip());
            settings.setValue("compactSidebar", compact);
        });
        sidebarToggle->setToolTip("Collapse sidebar");
        sidebarToggle->setAccessibleName("Collapse sidebar");
        layout->addWidget(sidebar);
        sidebarDivider = new QFrame; sidebarDivider->setObjectName("sidebarDivider"); sidebarDivider->setFrameShape(QFrame::VLine);
        sidebarDivider->setFixedWidth(1); layout->addWidget(sidebarDivider);
        pages = new QStackedWidget;
        layout->addWidget(pages, 1);
        buildExplore();
        buildInstalled();
        buildUpdates();
        compactSidebar->setChecked(settings.value("compactSidebar", false).toBool());
        activityDialog = new QDialog(this); activityDialog->setObjectName("activityPopup");
        activityDialog->setWindowTitle("Activity"); activityDialog->resize(800, 600);
        auto *activity = new QWidget;
        auto *activityLayout = new QVBoxLayout(activity);
        activityLayout->setContentsMargins(20, 16, 20, 24); activityLayout->setSpacing(16);
        activityTitle = label("Activity", 23, true);
        activityStatus = label("No package operation running.");
        activityHistory = new QListWidget; activityHistory->setObjectName("activityHistory"); activityHistory->setFrameShape(QFrame::NoFrame);
        activityHistory->setAccessibleName("Activity history"); activityHistory->setMinimumHeight(120); activityHistory->setSpacing(8);
        activityHistory->setStyleSheet("QListWidget { background: transparent; border: none; } QListWidget::item { background: palette(alternate-base); border: none; border-radius: 8px; }");
        activityLayout->addWidget(activityTitle);
        activityLayout->addWidget(activityStatus);
        auto *emptyActivity = label("No activity yet\nInstall an app to see activity"); emptyActivity->setObjectName("emptyActivity");
        activityLayout->addWidget(emptyActivity);
        activityLayout->addWidget(activityHistory, 1);
        terminalHost = new QWidget;
        terminalLayout = new QVBoxLayout(terminalHost);
        terminalLayout->setContentsMargins(0, 12, 0, 0);
        activityLayout->addWidget(terminalHost, 1);
        auto *back = button("Close", "window-close");
        auto *clear = button("Clear", "edit-clear"); clear->setObjectName("clearActivity");
        auto *clearOpacity = new QGraphicsOpacityEffect(clear); clear->setGraphicsEffect(clearOpacity);
        clear->setToolTip("Clear completed activity");
        auto *activityActions = new QHBoxLayout; activityActions->addWidget(back); activityActions->addWidget(clear); activityActions->addStretch();
        activityLayout->addLayout(activityActions);
        connect(clear, &QPushButton::clicked, this, [this] {
            for (auto &entry : previewOperations.activities) if (entry.progress == 100) entry.cleared = true;
            if (busy() && !packageActivityHistory.isEmpty()) { packageActivityHistory = {packageActivityHistory.last()}; packageJobs = {packageJobs.last()}; }
            else { packageActivityHistory.clear(); packageJobs.clear(); }
            refreshActivity();
        });
        connect(back, &QPushButton::clicked, activityDialog, &QDialog::hide);
        auto *popupLayout = new QVBoxLayout(activityDialog); popupLayout->setContentsMargins(0, 0, 0, 0); popupLayout->addWidget(activity);
        pages->addWidget(new QWidget); // Keep the existing app-details stack index.
        previewOperations.onChanged = [this] { refreshActivity(); };
        previewOperations.onOpen = [this](const QString &, const QString &name) { statusBar()->showMessage(name + " · no application launched", 4000); };
        if (inventoryEnabled) {  // The checks keep the simulated operations
            previewOperations.execute = [this](const QString &id, const QString &name, const QString &action) { return runOperation(id, name, action); };
            previewOperations.installedState = [this](const QString &id) { return installedApp(id); };
            previewOperations.onOpen = [this](const QString &id, const QString &name) { openInstalled(id, name); };
        }
        refreshActivity();
        appDetails = new AppDetailsPage;
        appDetails->operations = &previewOperations;
        appDetails->setObjectName("appDetailsPage");
        pages->addWidget(appDetails);
        history.append({pages->currentWidget()}); historyIndex = 0;
        appDetails->backend = &backend;
        explore->onOpen = [this](const CatalogApp &app) { openDetails(app); };
        explore->onRefresh = [this] { if (inventoryEnabled) backend.refresh(); };
        appDetails->onBack = [this] { moveHistory(-1); };
        appDetails->onOpenAddon = [this](const CatalogApp &app) { openDetails(app); };
        qApp->installEventFilter(this);
        connect(navigation, &QButtonGroup::idClicked, this, &Store::selectPage);
        connect(&backend, &Backend::installedChanged, this, [this] { renderInstalled(); settleOperation(); previewOperations.refreshButtons(); appDetails->refreshRemove();
            if (inventoryEnabled && !updatesChecked) checkForUpdates();  // Once at start, when the installed list is known
        });
        connect(&backend, &Backend::issue, this, [this](const QString &message) {
            statusBar()->showMessage(message);
        });
        statusBar()->hide();  // Messages stay readable for checks, but nothing is drawn at the bottom
        connect(&backend, &Backend::catalogReady, this, [this] {
            explore->notice->setText("Repository apps have no icons or descriptions yet. Install them with: sudo pacman -S --needed archlinux-appstream-data");
            explore->notice->setVisible(backend.osMetadataMissing);
            explore->setCatalog();
            appIndex.clear();
            for (int i = 0; i < backend.apps.size(); ++i) appIndex.insert(backend.apps[i].key, i);
            if (inventoryEnabled) backend.loadInstalled();  // So buttons of installed apps read "Open"
        });
        connect(&backend, &Backend::rankingsReady, this, [this] { explore->setCatalog(); });
        // Checks and previews use a small fixed catalogue instead of the live sources.
        if (loadCatalogue) backend.load();
        else if (!backend.loadFixture(WOOFSTORE_FIXTURES "/catalog.json")) qWarning("Cannot read the test catalogue in " WOOFSTORE_FIXTURES);
    }

    void selectPage(int page) {
        if (!navigation->button(page)) return;
        const bool enteringInstalled = page == 1 && pages->currentIndex() != 1;
        if (pages->currentWidget() != pages->widget(page)) pushHistory({pages->widget(page), {}, {}, page});
        if (enteringInstalled && inventoryEnabled) backend.loadInstalled();
    }

    void previewInstalled() {
        backend.installed.clear();
        for (const auto &entry : backend.apps) {
            if (entry.origin == MetadataOrigin::None) continue;
            App app; app.name = entry.name; app.summary = entry.summary; app.icon = catalogIcon(entry);
            for (const auto &package : entry.packages) if (package.source == Source::Flatpak) app.id = package.name;
            app.source = "Flatpak"; app.remote = "flathub"; app.scope = "system"; app.version = "sample"; backend.installed.append(app);
        }
        renderInstalled(); selectPage(1);
        statusBar()->clearMessage();
    }

    void previewUpdates(const QString &state) {
        selectPage(2);
        statusBar()->clearMessage();
        pendingUpdates.clear(); updateProblems.clear(); updateChecking = state == "checking"; updatesChecked = state != "checking";
        lastUpdateCheck = QTime(9, 41);
        if (state == "failed") updateProblems = {"Flatpak: the remote is not reachable."};
        if (state == "available") pendingUpdates = {{"org.kde.krita", "Krita", "5.2.9", "5.3.0", "Flatpak"}, {"org.gimp.GIMP", "GIMP", "3.0.4", "3.0.6", "Flatpak"},
            {"systemd", "systemd", "262-1", "262.1-1", "Arch"}, {"linux-cachyos", "linux-cachyos", "7.2.9-2", "7.2.11-1", "Arch"}};
        refreshUpdates();
    }
    void previewListSelection() {
        auto *list = pages->currentWidget() == explore ? explore->results : installedList;
        if (list->count()) { list->setCurrentRow(0); list->setFocus(); }
    }

    void showActivity() { jobFailed = false; lastResult = 0; refreshActivity(); activityDialog->show(); activityDialog->raise(); activityDialog->activateWindow(); }
    void previewActivity() { previewOperations.start("sample", "Krita", "Install"); showActivity(); }
    void refreshActivity() {
        if (!activityHistory) return;
        struct Record { QString name, state; int progress; bool unknown; };
        QList<Record> records;
        for (const auto &entry : packageActivityHistory) records.append({entry, "Package operation", 0, busy() && entry == packageActivityHistory.last()});
        for (const auto &activity : previewOperations.activities) {
            if (activity.cleared) continue;
            const QString state = activity.failed ? "Failed" : activity.progress == 100 ? "Complete" : activity.real ? QString("Running") : QString::number(activity.progress) + "%";
            records.append({activity.name, activity.action + " · " + state, activity.progress, false});
        }
        activityDialog->findChild<QLabel *>("emptyActivity")->setVisible(records.isEmpty());
        if (activityHistory->count() != records.size()) {
            activityHistory->clear();
            for (int i = 0; i < records.size(); ++i) {
                auto *item = new QListWidgetItem(activityHistory); item->setSizeHint(QSize(0, 76));
                auto *row = new QWidget; auto *layout = new QVBoxLayout(row); layout->setContentsMargins(14, 10, 14, 10); layout->setSpacing(5);
                auto *name = label("", 0, true); name->setObjectName("activityName");
                auto *state = label(""); state->setObjectName("activityState");
                auto *progress = new QProgressBar; progress->setObjectName("activityProgress"); progress->setTextVisible(false); progress->setFixedHeight(4);
                layout->addWidget(name); layout->addWidget(state); layout->addWidget(progress); activityHistory->setItemWidget(item, row);
                for (QWidget *part : {static_cast<QWidget *>(name), static_cast<QWidget *>(state), static_cast<QWidget *>(progress)}) part->setAttribute(Qt::WA_TransparentForMouseEvents);
                row->installEventFilter(this);
            }
        }
        for (int i = 0; i < records.size(); ++i) {
            const auto &record = records[i]; auto *row = activityHistory->itemWidget(activityHistory->item(i));
            const bool opens = i < packageJobs.size() && !packageJobs[i].title.isEmpty();
            row->setProperty("jobIndex", opens ? i : -1);
            row->setCursor(opens ? Qt::PointingHandCursor : Qt::ArrowCursor); row->setToolTip(opens ? "Show the details of this operation" : QString());
            row->findChild<QLabel *>("activityName")->setText(record.name);
            row->findChild<QLabel *>("activityState")->setText(record.state);
            auto *progress = row->findChild<QProgressBar *>("activityProgress");
            progress->setRange(0, record.unknown ? 0 : 100); progress->setValue(record.progress);
            progress->setVisible(record.unknown || record.progress > 0);
        }
        terminalHost->setVisible(bool(part));
        const bool completed = std::any_of(previewOperations.activities.cbegin(), previewOperations.activities.cend(), [](const auto &entry) { return !entry.cleared && entry.progress == 100; });
        auto *clear = activityDialog->findChild<QPushButton *>("clearActivity");
        const bool canClear = completed || packageActivityHistory.size() > (busy() ? 1 : 0);
        clear->setEnabled(canClear); clear->setCursor(canClear ? Qt::PointingHandCursor : Qt::ArrowCursor);
        static_cast<QGraphicsOpacityEffect *>(clear->graphicsEffect())->setOpacity(canClear ? 1. : .4);
        if (!busy()) activityStatus->setText(previewOperations.running() ? "No package changes" : "Activity history");
        activityButton->setResult(lastResult);
        activityButton->attention = bool(part) || jobFailed;  // A terminal waiting for input, or a failure nobody has looked at activityButton->update();
        activityButton->busy = busy() || previewOperations.running(); activityButton->indeterminate = busy();
        activityButton->progress = previewOperations.activities.isEmpty() ? 0 : previewOperations.progress();
        activityButton->setToolTip(busy() ? "Package activity running · progress unavailable" : previewOperations.running() ? QString("Activity · %1%").arg(activityButton->progress) : "Show activity");
        activityButton->update();
    }

    static bool acceptsFile(const QString &kind, const QUrl &url) {
        if (!url.isLocalFile()) return false;
        const QFileInfo file(url.toLocalFile()); const QString suffix = file.suffix().toLower();
        if (!file.isFile() || !file.isReadable()) return false;
        return kind == "AppImage" ? suffix == "appimage" : suffix == "flatpak" || suffix == "flatpakref";
    }
    // The same progress window as every other install; AppImages are copied by a helper run of this program, off the interface thread.
    void installFiles(const QString &kind, const QList<QUrl> &urls) {
        QStringList files;
        for (const auto &url : urls) if (acceptsFile(kind, url)) files << QFileInfo(url.toLocalFile()).canonicalFilePath();
        if (files.isEmpty()) { QMessageBox::information(this, "Add " + kind, kind == "AppImage" ? "Choose a .AppImage file." : "Choose a .flatpak or .flatpakref file."); return; }
        const QString title = "Install " + (files.size() == 1 ? QFileInfo(files[0]).completeBaseName() : QString("%1 files").arg(files.size()));
        if (kind == "AppImage") {
            QStringList arguments; for (const QString &file : files) arguments << "--import-appimage" << file;
            startJob(title, QCoreApplication::applicationFilePath(), arguments, {}, {"Prepare", "Copy", "Add to menu"},
                [](const QString &text) { return text.contains("Adding to the app menu") ? 2 : text.contains("Copying") ? 1 : 0; });
        } else {
            const QString script = "for f in \"$@\"; do case \"$f\" in *.flatpakref) flatpak install --system -y --noninteractive --from \"$f\";; "
                "*) flatpak install --system -y --noninteractive --bundle \"$f\";; esac || exit; done";
            startJob(title, "bash", QStringList{"-c", script, "woof-store-flatpak-files"} + files);
        }
    }
    void removeAppImage(const App &app) {
        if (QMessageBox::question(this, "Remove " + app.name, "This deletes the AppImage file:\n" + app.path) != QMessageBox::Yes) return;
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        bool ok;
        if (app.path.startsWith(data + "/WoofStore/appimages/")) ok = QFile::remove(app.path);
        else ok = QFile::moveToTrash(app.path);  // A file the user keeps elsewhere goes to the trash, not away for good
        if (ok && app.launcher.startsWith(QDir::homePath())) QFile::remove(app.launcher);
        if (!ok) QMessageBox::warning(this, "Cannot remove " + app.name, "The file could not be deleted: " + app.path);
        backend.loadInstalled();
    }

    // closeOnSuccess: package operations close themselves when they succeed; a failure keeps the output until Enter.
    bool startActivity(const QString &title, const QString &program, const QStringList &arguments = {}, const QString &cwd = {}, bool closeOnSuccess = false) {
        if (part) { QMessageBox::information(this, "Operation already running", "Finish the current activity before starting another package operation."); showActivity(); return false; }
        const QString executable = QStandardPaths::findExecutable(program);
        if (executable.isEmpty()) { QMessageBox::warning(this, "Required program missing", "Install " + program + " and try again."); return false; }
        const auto metadata = KPluginMetaData::findPluginById("kf6/parts", "konsolepart");
        const auto result = KPluginFactory::instantiatePlugin<KParts::ReadOnlyPart>(metadata, this);
        if (!result.plugin) { QMessageBox::critical(this, "Embedded terminal unavailable", "Install the Qt 6 version of Konsole, then restart the store.\n\n" + result.errorText); return false; }
        part = result.plugin;
        auto *terminal = qobject_cast<TerminalInterface *>(part.data());
        if (!terminal) { delete part; QMessageBox::critical(this, "Unsupported terminal component", "KonsolePart does not provide the required terminal interface."); return false; }
        terminalLayout->addWidget(part->widget());
        activityTitle->setText(title);
        packageActivityHistory.append(title + " · Running · progress unavailable"); packageJobs.append(JobRecord{});
        activityStatus->setText("Answer questions directly below. Passwords are handled by the package manager. Keep this window open until the process finishes.");
        showActivity();
        refreshActivity();
        connect(part, &QObject::destroyed, this, [this] {
            part = nullptr;
            if (!packageActivityHistory.isEmpty()) packageActivityHistory.last() = activityTitle->text() + " · Ended · check command exit code";
            activityStatus->setText("Activity ended. Check the exit code in its output before retrying.");
            refreshActivity();
            settling = true;
            backend.loadInstalled();
        });
        // Fixed wrapper; package names, paths and arguments are positional data, never shell code.
        const QString wrapper = QString("cd -- \"$1\" || exit 1; shift; \"$@\"; code=$?; ") + (closeOnSuccess ? "if [ \"$code\" -eq 0 ]; then printf '\\nDone.'; sleep 1; exit 0; fi; " : "") + "printf '\\nCommand exit code: %s\\nPress Enter to close this activity.' \"$code\"; IFS= read -r answer; exit \"$code\"";
        // KonsolePart expects argv[0] in its argument list (unlike QProcess).
        QStringList args = {"/usr/bin/bash", "-c", wrapper, "woof-store-activity", cwd.isEmpty() ? QDir::homePath() : cwd, executable};
        args.append(arguments);
        terminal->startProgram("/usr/bin/bash", args);
        part->widget()->setFocus();
        return true;
    }

    // The app and package a button id stands for: the source chosen on the details page, else Repo, Flatpak, AUR.
    bool resolveOperation(const QString &key, CatalogApp *app, Package *package) const {
        if (appIndex.contains(key)) *app = backend.apps[appIndex.value(key)];
        else if (const auto *row = installedRow(key)) {  // A row of the Installed page: package name or Flatpak ID
            app->key = key; app->name = row->name;
            if (row->source != "Flatpak" && row->source != "Arch" && row->source != "AUR / local") return false;  // AppImages and other apps have no package to operate on
            const Source kind = row->source == "Flatpak" ? Source::Flatpak : row->source == "Arch" ? Source::Repo : Source::Aur;
            app->packages = {{kind, row->id, kind == Source::Flatpak ? row->remote : kind == Source::Repo ? QString("arch") : QString("aur"), row->version, 0}};
        } else if (key.startsWith("aur:")) { app->key = key; app->name = key.mid(4); app->packages = {{Source::Aur, key.mid(4), "aur", {}, 0}}; }
        else return false;
        if (app->packages.isEmpty()) return false;
        // A Flatpak needs a full application ID (org.kde.krita); a bare word would make flatpak ask which of many refs is meant.
        static const QRegularExpression appId("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$");
        auto usable = [](const Package &candidate) { return candidate.source != Source::Flatpak || appId.match(candidate.name).hasMatch(); };
        if (appDetails->currentId == key)
            for (const auto &candidate : app->packages) if (usable(candidate) && AppDetailsPage::packageLabel(candidate) == appDetails->sourceButton->text()) { *package = candidate; return true; }
        for (Source wanted : {Source::Repo, Source::Flatpak, Source::Aur})
            for (const auto &candidate : app->packages) if (candidate.source == wanted && usable(candidate)) { *package = candidate; return true; }
        return false;
    }
    const App *installedRow(const QString &id) const {
        for (const auto &row : backend.installed) if (row.id == id) return &row;
        return nullptr;
    }
    bool installedApp(const QString &key) const {
        CatalogApp app; Package package;
        if (!resolveOperation(key, &app, &package)) return false;
        for (const auto &candidate : app.packages) if (backend.installedPackage(candidate)) return true;
        return false;
    }
    void openInstalled(const QString &key, const QString &name) {
        if (const auto *row = installedRow(key); row && !row->launcher.isEmpty() && !QStandardPaths::findExecutable("gio").isEmpty()) { QProcess::startDetached("gio", {"launch", row->launcher}); return; }
        CatalogApp app; Package package;
        if (!resolveOperation(key, &app, &package)) return;
        for (const auto &candidate : app.packages) if (backend.installedPackage(candidate)) package = candidate;
        if (package.source == Source::Flatpak) { QProcess::startDetached("flatpak", {"run", package.name}); return; }
        const QString launcher = app.desktopId.isEmpty() ? QString() : QStandardPaths::locate(QStandardPaths::ApplicationsLocation, app.desktopId);
        if (launcher.isEmpty() || QStandardPaths::findExecutable("gio").isEmpty()) { statusBar()->showMessage(name + " · no launcher found", 4000); return; }
        QProcess::startDetached("gio", {"launch", launcher});
    }
    // Runs a package operation without a terminal. Passwords come from the system: polkit for pkexec, a GUI askpass for sudo.
    bool startJob(const QString &title, const QString &program, const QStringList &arguments, const QString &cwd = {}, const QStringList &stepNames = {}, const std::function<int(const QString &)> &stepOf = {}) {
        if (busy()) { QMessageBox::information(this, "Operation already running", "Finish the current activity before starting another package operation."); showActivity(); return false; }
        const QString executable = QStandardPaths::findExecutable(program);
        if (executable.isEmpty()) { QMessageBox::warning(this, "Required program missing", "Install " + program + " and try again."); return false; }
        // sudo has no terminal here, so it asks through this helper instead.
        const QString askpass = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/askpass.sh";
        QDir().mkpath(QFileInfo(askpass).absolutePath());
        QFile helper(askpass);
        if (helper.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            helper.write("#!/bin/sh\nif command -v kdialog >/dev/null; then exec kdialog --title WoofStore --password \"${1:-Password}\"; fi\nexec zenity --password --title=WoofStore\n");
            helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        auto *process = new QProcess(this);
        job = process;
        process->setProcessChannelMode(QProcess::MergedChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SUDO_ASKPASS", askpass); environment.insert("LC_ALL", "C.UTF-8");
        process->setProcessEnvironment(environment);
        if (!cwd.isEmpty()) process->setWorkingDirectory(cwd);
        const bool removing = title.startsWith("Remove");
        const QStringList names = !stepNames.isEmpty() ? stepNames : removing ? QStringList{"Prepare", "Remove"} : QStringList{"Prepare", "Download", "Install"};
        // Without a program-specific rule, read the step from the words flatpak and pacman print.
        const auto stepFor = stepOf ? stepOf : [names](const QString &text) {
            const QString lower = text.toLower();
            if (lower.contains("install") || lower.contains("remov") || lower.contains("uninstall")) return int(names.size()) - 1;
            return lower.contains("download") || lower.contains("fetch") || lower.contains("resolving") ? 1 : 0;
        };
        auto *dialog = new JobDialog(title, names, this); dialog->setAttribute(Qt::WA_DeleteOnClose);
        jobDialog = dialog;
        connect(dialog->close, &QPushButton::clicked, this, [this] { jobFailed = false; refreshActivity(); });
        packageActivityHistory.append(title + " · Running · progress unavailable"); packageJobs.append({title, names, 0, {}, false, false});
        lastResult = 0;
        refreshActivity();
        connect(process, &QProcess::readyRead, this, [process, dialog, stepFor] {
            const QString text = QString::fromUtf8(process->readAll());
            dialog->feed(text);
            dialog->advanceTo(stepFor(text));
        });
        connect(process, &QProcess::finished, this, [this, process, dialog, title, names](int code, QProcess::ExitStatus status) {
            const bool ok = status == QProcess::NormalExit && code == 0;
            job = nullptr; process->deleteLater();
            if (!packageActivityHistory.isEmpty()) packageActivityHistory.last() = title + (ok ? " · Done" : " · Failed");
            dialog->finish(ok, ok ? "Done" : "It did not finish. Open the details to see why.");
            if (!packageJobs.isEmpty()) packageJobs.last() = {title, names, dialog->step, dialog->log->toPlainText(), true, ok};
            if (ok) QTimer::singleShot(1200, dialog, &QDialog::close); else dialog->show();
            if (!ok) jobFailed = true;
            lastResult = ok ? 1 : 2;
            refreshActivity();
            if (inventoryEnabled) { settling = true; backend.loadInstalled(); }
            if (inventoryEnabled && title.startsWith("Update")) QTimer::singleShot(0, this, &Store::checkForUpdates);
        });
        // setsid: no controlling terminal, so sudo uses the askpass dialog; -w keeps the exit code.
        process->start("setsid", QStringList{"-w", executable} + arguments);
        dialog->show();
        return true;
    }
    // AppStream's origin is not always a remote name ("flatpak"); flatpak would then read it as the app to install.
    static QString flatpakRemote(const QString &origin) {
        QProcess remotes; remotes.start("flatpak", {"remotes", "--system", "--columns=name"});
        remotes.waitForFinished(5000);
        return QString::fromUtf8(remotes.readAllStandardOutput()).split('\n').contains(origin) ? origin : QString("flathub");
    }
    // Starts the real operation for a button. false = not a catalogue app, the caller keeps the simulation.
    bool runOperation(const QString &key, const QString &name, const QString &action) {
        if (const auto *row = installedRow(key); row && row->source == "AppImage") {  // Files, not packages
            if (action == "Remove") removeAppImage(*row);
            return true;
        }
        CatalogApp app; Package package;
        if (!resolveOperation(key, &app, &package)) return false;
        if (busy()) { QMessageBox::information(this, "Operation already running", "Finish the current activity before starting another package operation."); showActivity(); return true; }
        if (action == "Remove") { removeApp(key, name, app); return true; }
        const bool again = action == "Reinstall";
        if (package.source == Source::Flatpak) {
            QStringList arguments{"install", "--system", "-y", "--noninteractive"}; if (again) arguments << "--reinstall";
            if (startJob((again ? "Reinstall " : "Install ") + name, "flatpak", arguments + QStringList{flatpakRemote(package.repo), package.name})) beginPending(key, name, action, package);
        } else if (package.source == Source::Repo) {
            if (startJob((again ? "Reinstall " : "Install ") + name, "pkexec", QStringList{"pacman", "-S", "--noconfirm"} + (again ? QStringList{} : QStringList{"--needed"}) + QStringList{package.name})) beginPending(key, name, action, package);
        } else installAur(key, name, action, package);
        return true;
    }
    void beginPending(const QString &key, const QString &name, const QString &action, const Package &package) {
        pendingOp = {key, name, action, package, true};
        previewOperations.begin(key, name, action);
    }
    void settleOperation() {
        if (!settling || busy() || !pendingOp.active) return;
        settling = false; pendingOp.active = false;
        const bool installed = backend.installedPackage(pendingOp.package);
        const bool ok = installed == (pendingOp.action != "Remove");
        previewOperations.finish(pendingOp.key, ok);
        if (ok && !pendingOp.leftovers.isEmpty()) {  // The package is gone; now the files the user chose
            int removed = 0;
            for (const auto &path : pendingOp.leftovers) removed += deleteLeftover(QDir::homePath(), path);
            statusBar()->showMessage(QString("%1 removed · %2 leftover item(s) deleted").arg(pendingOp.name).arg(removed), 6000);
        }
        pendingOp.leftovers.clear();
    }
    void installAur(const QString &key, const QString &name, const QString &action, const Package &package) {
        if (QStandardPaths::findExecutable("pkgctl").isEmpty()) {
            QMessageBox::information(this, "Clean chroot tools missing", "AUR packages are built in a clean chroot with pkgctl, from the devtools package.\n\nInstall it, then try again:\n\nsudo pacman -S --needed devtools");
            return;
        }
        backend.aurInfo(package.name, [this, key, name, action, package](AurInfo info, QString failure) {
            if (!failure.isEmpty()) { QMessageBox::warning(this, "Cannot install " + package.name, failure); return; }
            App request; request.base = info.base; request.name = package.name;
            backend.prepareAur(request, [this, key, name, action, package, info](QString recipe, QString commit, QString failure) {
                if (!failure.isEmpty()) { QMessageBox::warning(this, "Cannot review " + package.name, failure); return; }
                const QString store = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/reviewed";
                QString previousCommit;
                const QString previous = loadReviewed(store, info.base, &previousCommit);
                auto *dialog = new AurReviewDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->onApprove = [this, key, name, action, package, info, recipe, commit, store] {
                    QString error;
                    if (!backend.reviewedFilesUnchanged(&error)) { QMessageBox::warning(this, "Build files changed", error); return; }
                    saveReviewed(store, info.base, commit, recipe);
                    // pkgctl builds in a clean chroot; only the exact built package is installed, as root.
                    const QString script = "pkgctl build || exit; found=; for f in ./*.pkg.tar.*; do case \"$f\" in *.sig) continue;; esac; "
                        "[ \"$(pacman -Qpq \"$f\")\" = \"$1\" ] && found=$f; done; [ -n \"$found\" ] || { echo \"No built package named $1\"; exit 1; }; "
                        "found=$(realpath \"$found\"); echo \"==> Installing package\"; pkexec pacman -U --noconfirm $2 \"$found\"";
                    if (startJob("Install " + name + " from the AUR", "bash", {"-c", script, "woof-store-aur", package.name, action == "Reinstall" ? "" : "--needed"}, backend.reviewPath(),
                        {"Prepare", "Download", "Build", "Install"}, [](const QString &text) { return text.contains("Installing package") ? 3 : text.contains("Making package") || text.contains("Starting build") ? 2 : text.contains("Retrieving sources") ? 1 : 0; })) beginPending(key, name, action, package);
                };
                dialog->showReview(info, recipe, previous);
                dialog->open();
            });
        });
    }
    void removeApp(const QString &key, const QString &name, const CatalogApp &app) {
        Package package;
        bool found = false;
        for (const auto &candidate : app.packages) if (backend.installedPackage(candidate)) { package = candidate; found = true; }
        if (!found) { statusBar()->showMessage(name + " is not installed"); return; }
        const QString home = QDir::homePath();
        QList<Leftover> leftovers = findLeftovers(home, {package.name, app.name});
        if (package.source == Source::Flatpak) leftovers = flatpakData(home, package.name) + leftovers;
        const bool flatpak = package.source == Source::Flatpak;
        RemoveDialog dialog(name, {{flatpak ? "Also remove runtimes no other app uses" : "Also remove dependencies nothing else needs", true}}, leftovers, home, this);
        if (dialog.exec() != QDialog::Accepted) return;
        bool started;
        if (flatpak) {
            const QString script = dialog.options[0]->isChecked() ? "flatpak uninstall -y --noninteractive \"$1\" && flatpak uninstall -y --noninteractive --unused" : "flatpak uninstall -y --noninteractive \"$1\"";
            started = startJob("Remove " + name, "bash", {"-c", script, "woof-store-remove", package.name});
        } else started = startJob("Remove " + name, "pkexec", {"pacman", dialog.options[0]->isChecked() ? "-Rns" : "-Rn", "--noconfirm", package.name});
        if (!started) return;
        beginPending(key, name, "Remove", package);
        pendingOp.leftovers = dialog.chosenPaths();
    }

protected:
    // The compositor owns the window edge; omit the themed content-top frame.
    void paintEvent(QPaintEvent *) override {}
    void closeEvent(QCloseEvent *event) override {
        if (busy()) {
            showActivity();
            QMessageBox::information(this, "Package operation active", "Finish the activity before closing the store. Interrupting a package transaction can leave your system partly updated.");
            event->ignore();
        } else QMainWindow::closeEvent(event);
    }
private:
    PreviewOperations previewOperations;
    ActivityButton *activityButton = nullptr;
    QDialog *activityDialog = nullptr;
    QListWidget *activityHistory = nullptr;
    QStringList packageActivityHistory;
public:
    // What the details window of each package operation showed, parallel to packageActivityHistory; title is empty for terminal jobs.
    struct JobRecord { QString title; QStringList steps; int step = 0; QString log; bool finished = false, ok = false; };
    QList<JobRecord> packageJobs;
    void openJob(int index) {
        if (index < 0 || index >= packageJobs.size()) return;
        if (index == packageJobs.size() - 1 && jobDialog) { jobDialog->show(); jobDialog->raise(); jobDialog->activateWindow(); return; }
        const JobRecord &record = packageJobs[index];
        if (!record.finished) return;
        auto *dialog = new JobDialog(record.title, record.steps, this); dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->feed(record.log); dialog->showStep(record.step);
        dialog->finish(record.ok, record.ok ? "Done" : "It did not finish. Open the details to see why.");
        connect(dialog->close, &QPushButton::clicked, dialog, &QDialog::close);
        dialog->show();
    }
private:
    QPushButton *updateAll = nullptr;
    QSettings settings;
    QWidget *sidebar = nullptr;
    QFrame *sidebarDivider = nullptr;
    Apps displayedInstalled;
    QAction *compactSidebar = nullptr;
    QVariantAnimation sidebarMotion;
    QButtonGroup *navigation = nullptr;
    QStackedWidget *pages = nullptr;
    ExplorePage *explore = nullptr;
    AppDetailsPage *appDetails = nullptr;
    struct HistoryEntry {
        QWidget *page = nullptr;
        CatalogApp app{};
        QString platform{};
        int section = 0, scroll = 0;
        QPointer<QWidget> focus{};
    };
    QList<HistoryEntry> history;
    int historyIndex = -1;
    void saveHistory() {
        if (historyIndex < 0) return;
        auto &entry = history[historyIndex];
        if (entry.page == appDetails) entry.platform = appDetails->sourceButton->text();
        if (auto *page = qobject_cast<QScrollArea *>(entry.page)) entry.scroll = page->verticalScrollBar()->value();
        entry.focus = QApplication::focusWidget();
    }
    void showHistory(bool fresh = false) {
        const auto &entry = history[historyIndex];
        if (entry.page == appDetails) {
            if (fresh || appDetails->currentId != entry.app.key || appDetails->sourceButton->text() != entry.platform)
                appDetails->showApp(entry.app, entry.platform);
            const QString section = entry.section == 1 ? "Installed" : "Explore";
            appDetails->back->setText(section); appDetails->back->setAccessibleName("Back to " + section);
            appDetails->back->setToolTip("Back to " + section + " (Alt+Left)");
        }
        navigation->button(entry.section)->setChecked(true); pages->setCurrentWidget(entry.page);
        if (auto *page = qobject_cast<QScrollArea *>(entry.page)) page->verticalScrollBar()->setValue(entry.scroll);
        if (!fresh && entry.page != appDetails && entry.focus && entry.focus->isVisible()) entry.focus->setFocus(Qt::OtherFocusReason);
        else entry.page->setFocus(Qt::OtherFocusReason);
    }
    void pushHistory(HistoryEntry entry) {
        saveHistory();
        while (history.size() > historyIndex + 1) history.removeLast();
        history.append(entry); historyIndex = history.size() - 1; showHistory(true);
    }
    void moveHistory(int direction) {
        const int next = historyIndex + direction;
        if (next < 0 || next >= history.size()) return;
        saveHistory(); historyIndex = next; showHistory();
    }
    void openDetails(const CatalogApp &app, const QString &platform = {}) {
        pushHistory({appDetails, app, platform, navigation->checkedId()});
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(object);
        if (event->type() == QEvent::MouseButtonRelease && widget && widget->property("jobIndex").isValid() && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            openJob(widget->property("jobIndex").toInt()); return true;
        }
        if (widget && widget->window() == this && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseButtonRelease)) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::BackButton || mouse->button() == Qt::ForwardButton) {
                if (event->type() != QEvent::MouseButtonRelease) moveHistory(mouse->button() == Qt::BackButton ? -1 : 1);
                event->accept(); return true;
            }
        }
        // Typing anywhere on Explore starts a search, so the first letters are not lost.
        if (event->type() == QEvent::KeyPress && widget && widget->window() == this && pages->currentWidget() == explore && !QApplication::activeModalWidget()) {
            auto *key = static_cast<QKeyEvent *>(event);
            const QString typed = key->text();
            auto *focused = QApplication::focusWidget();
            const bool editing = qobject_cast<QLineEdit *>(focused) || qobject_cast<QAbstractSpinBox *>(focused) || qobject_cast<QComboBox *>(focused) || qobject_cast<QTextEdit *>(focused) || qobject_cast<QPlainTextEdit *>(focused);
            if (!editing && !typed.isEmpty() && typed[0].isPrint() && !typed[0].isSpace() && !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
                explore->search->setFocus(); explore->search->setText(explore->search->text() + typed);
                return true;
            }
        }
        return QMainWindow::eventFilter(object, event);
    }
    QLineEdit *installedSearch = nullptr;
    QComboBox *filter = nullptr, *installedFilter = nullptr;
    QLabel *installedLabel = nullptr;
    QWidget *installedBody = nullptr;
    AppList *installedList = nullptr;
    QLabel *installedEmpty = nullptr;
    bool inventoryEnabled = true;
    QVBoxLayout *terminalLayout = nullptr;
    QWidget *terminalHost = nullptr;
    QLabel *activityTitle = nullptr, *activityStatus = nullptr;
    QLineEdit *updateStatus = nullptr;
    QLabel *updatesIcon = nullptr, *updatesTitle = nullptr, *updatesHint = nullptr;
    QPushButton *checkUpdates = nullptr;
    QWidget *updatesEmpty = nullptr, *updatesPanel = nullptr;
    QVBoxLayout *updatesRows = nullptr;
public:
    QList<UpdateEntry> pendingUpdates;  // Found by the last check
    QStringList updateProblems;
    bool updateChecking = false, updatesChecked = false;
    QTime lastUpdateCheck;
private:
    QPointer<KParts::ReadOnlyPart> part;
    QPointer<QProcess> job;  // A package operation running in the background, shown by jobDialog
    QPointer<JobDialog> jobDialog;
    bool busy() const { return part || job; }
    bool jobFailed = false;
    int lastResult = 0;  // Shown on the Activity button until the Activity window is opened
    QHash<QString, int> appIndex;  // CatalogApp::key to index in backend.apps
    struct PendingOperation { QString key, name, action; Package package; bool active = false; QStringList leftovers; } pendingOp;
    bool settling = false;

    QWidget *scrollPage(QVBoxLayout **content, QScrollArea *supplied = nullptr) {
        auto *scroll = supplied ? supplied : new PageScroll;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        *content = new QVBoxLayout(body);
        (*content)->setContentsMargins(18, 16, 18, 18);
        (*content)->setSpacing(12);
        scroll->setWidget(body);
        pages->addWidget(scroll);
        return body;
    }

    void buildExplore() {
        explore = new ExplorePage(&backend);
        explore->setOperations(&previewOperations);
        explore->setObjectName("explorePrototype");
        filter = explore->source;
        pages->addWidget(explore);
    }

    void buildInstalled() {
        QVBoxLayout *content;
        installedBody = scrollPage(&content);
        installedBody->setObjectName("installedPageBody");
        pages->widget(pages->count() - 1)->setObjectName("installedPage");
        content->setContentsMargins(20, 16, 20, 24); content->setSpacing(16);
        auto *sticky = new StickyBar(static_cast<QScrollArea *>(pages->widget(pages->count() - 1)), content);
        sticky->column->addWidget(label("Installed", 23, true));
        auto *searchTray = new QWidget;
        auto *searchRow = new QHBoxLayout(searchTray);
        searchRow->setContentsMargins(0, 0, 0, 0);
        searchRow->setSpacing(8);
        installedSearch = new QLineEdit; installedSearch->setObjectName("installedSearch");
        installedSearch->setPlaceholderText("Search installed software");
        installedSearch->setClearButtonEnabled(true);
        installedSearch->setMinimumHeight(38);
        installedSearch->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition);
        installedSearch->setAccessibleName("Search installed software");
        installedFilter = new QComboBox; installedFilter->setObjectName("installedFilter");
        installedFilter->addItems({"All sources", "Flatpak", "Arch", "AUR / local", "AppImage", "Other"});
        installedFilter->setMinimumHeight(38);
        installedFilter->setAccessibleName("Installed software source");
        auto *refresh = new QPushButton(lucideIcon("refresh", 20), {}); refresh->setObjectName("refreshInstalled");
        refresh->setFlat(true); refresh->setFixedSize(38, 38); refresh->setToolTip("Refresh the list"); refresh->setAccessibleName("Refresh installed software"); refresh->setCursor(Qt::PointingHandCursor);
        searchRow->addWidget(installedSearch, 1);
        searchRow->addWidget(installedFilter);
        searchRow->addWidget(refresh);
        sticky->column->addWidget(searchTray);
        auto chooseFiles = [this](const QString &kind) {
            const auto files = QFileDialog::getOpenFileNames(this, "Add " + kind, QDir::homePath(), kind == "AppImage" ? "AppImages (*.AppImage *.appimage)" : "Flatpak files (*.flatpak *.flatpakref)");
            QList<QUrl> urls; for (const auto &file : files) urls.append(QUrl::fromLocalFile(file));
            if (!urls.isEmpty()) installFiles(kind, urls);
        };
        auto *cards = new QHBoxLayout; cards->setSpacing(16);
        for (const QString kind : {"AppImage", "Flatpak"}) {
            auto *card = new DropCard("Add " + kind, kind, kind == "AppImage" ? "Drop a file here or click to choose" : "Drop a .flatpak or .flatpakref file here");
            card->setObjectName("add" + kind);
            card->accepts = [kind](const QUrl &url) { return acceptsFile(kind, url); };
            card->onFiles = [this, kind](const QList<QUrl> &files) { installFiles(kind, files); };
            connect(card, &QAbstractButton::clicked, this, [chooseFiles, kind] { chooseFiles(kind); });
            cards->addWidget(card, 1);
        }
        sticky->column->addLayout(cards);
        sticky->fit();
        installedLabel = label("Loading installed software…", 0, true);
        content->addWidget(installedLabel);
        installedList = new AppList; installedList->setObjectName("installedApps"); installedList->setAccessibleName("Installed apps, alphabetically sorted"); content->addWidget(installedList);
        installedEmpty = label("No installed app matches."); content->addWidget(installedEmpty); installedEmpty->hide();
        connect(installedList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { openInstalledItem(item); });
        connect(installedList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { openInstalledItem(item); });
        content->addStretch();
        connect(installedSearch, &QLineEdit::textChanged, this, &Store::renderInstalled);
        connect(installedFilter, &QComboBox::currentIndexChanged, this, &Store::renderInstalled);
        connect(refresh, &QPushButton::clicked, &backend, &Backend::loadInstalled);
    }

    // Updates: a list only when there is something to update, otherwise a short positive message.
    void buildUpdates() {
        QVBoxLayout *content;
        auto *body = scrollPage(&content); body->setObjectName("updatesPageBody");
        content->setContentsMargins(20, 16, 20, 24); content->setSpacing(16);
        auto *sticky = new StickyBar(static_cast<QScrollArea *>(pages->widget(pages->count() - 1)), content);
        sticky->column->addWidget(label("Updates", 23, true));
        // Same place as the search row of Installed: what the last check found, and the button to run it again.
        auto *statusRow = new QHBoxLayout; statusRow->setSpacing(8);
        updateStatus = new QLineEdit; updateStatus->setObjectName("updateStatus"); updateStatus->setReadOnly(true);  // Looks like the search bar of Installed
        updateStatus->setFocusPolicy(Qt::NoFocus); updateStatus->setMinimumHeight(38); updateStatus->setCursor(Qt::ArrowCursor);
        checkUpdates = new QPushButton(lucideIcon("refresh", 20), {}); checkUpdates->setObjectName("checkUpdates");
        checkUpdates->setFlat(true); checkUpdates->setFixedSize(38, 38); checkUpdates->setToolTip("Check for updates"); checkUpdates->setAccessibleName("Check for updates"); checkUpdates->setCursor(Qt::PointingHandCursor);
        statusRow->addWidget(updateStatus, 1); statusRow->addWidget(checkUpdates);
        sticky->column->addLayout(statusRow);
        sticky->fit();
        updatesEmpty = new QWidget; updatesEmpty->setObjectName("updatesEmpty"); updatesEmpty->setMinimumHeight(340);
        auto *emptyLayout = new QVBoxLayout(updatesEmpty); emptyLayout->setSpacing(10); emptyLayout->setAlignment(Qt::AlignCenter);
        updatesIcon = new QLabel; updatesIcon->setAlignment(Qt::AlignCenter);
        updatesTitle = label("", 18, true); updatesTitle->setObjectName("updatesTitle"); updatesTitle->setAlignment(Qt::AlignCenter);
        updatesHint = label(""); updatesHint->setObjectName("updatesHint"); updatesHint->setAlignment(Qt::AlignCenter); updatesHint->setMinimumWidth(380); updatesHint->setForegroundRole(QPalette::PlaceholderText);
        for (QWidget *part : {static_cast<QWidget *>(updatesIcon), static_cast<QWidget *>(updatesTitle), static_cast<QWidget *>(updatesHint)}) emptyLayout->addWidget(part);
        content->addWidget(updatesEmpty, 1);
        updatesPanel = new QWidget; updatesPanel->setObjectName("updateResults");
        updatesRows = new QVBoxLayout(updatesPanel); updatesRows->setContentsMargins(0, 0, 0, 0); updatesRows->setSpacing(16);
        content->addWidget(updatesPanel); content->addStretch();
        updateAll = new QPushButton(QIcon::fromTheme("system-software-update"), "Update all"); updateAll->setObjectName("updateAll");
        updateAll->setMinimumHeight(44); updateAll->setDefault(true); updateAll->setCursor(Qt::PointingHandCursor);
        new StickyFooter(static_cast<QScrollArea *>(pages->widget(pages->count() - 1)), content, updateAll);
        connect(checkUpdates, &QPushButton::clicked, this, &Store::checkForUpdates);
        connect(updateAll, &QPushButton::clicked, this, [this] { startJob("Update apps", "flatpak", {"update", "-y", "--noninteractive"}); });
        refreshUpdates();
    }

    void checkForUpdates() {
        if (updateChecking) return;
        updateChecking = true; refreshUpdates();
        backend.checkUpdates([this](QList<UpdateEntry> found, QStringList problems) {
            updateChecking = false; updatesChecked = true; lastUpdateCheck = QTime::currentTime();
            pendingUpdates = found; updateProblems = problems;
            refreshUpdates();
        });
    }

    void refreshUpdates() {
        checkUpdates->setEnabled(!updateChecking);
        updateStatus->setText(updateChecking ? "Checking…" : updatesChecked ? "Checked at " + lastUpdateCheck.toString("HH:mm") : QString());
        const bool listed = !pendingUpdates.isEmpty();
        updatesPanel->setVisible(listed); updatesEmpty->setVisible(!listed); updateAll->setVisible(listed);
        static_cast<NavButton *>(navigation->button(2))->badge = int(pendingUpdates.size()); navigation->button(2)->update();
        if (!listed) {
            const bool failed = updatesChecked && !updateProblems.isEmpty();
            QString glyph = "circle-check"; QColor color("#27ae60");
            if (updateChecking) { glyph = "refresh"; color = palette().color(QPalette::PlaceholderText); updatesTitle->setText("Checking for updates…"); updatesHint->setText({}); }
            else if (failed) { glyph = "circle-x"; color = QColor("#da4453"); updatesTitle->setText("Couldn't check for updates"); updatesHint->setText(updateProblems.join('\n')); }
            else if (!updatesChecked) { updatesTitle->setText("Updates haven't been checked yet"); updatesHint->setText("Use the refresh button above."); glyph = "refresh"; color = palette().color(QPalette::PlaceholderText); }
            else { updatesTitle->setText("You're all up to date"); updatesHint->setText("Your apps and system packages are current."); }
            QPixmap pixmap(144, 144); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap); drawLucide(painter, glyph, QRectF(0, 0, 72, 72), color, 1.4); painter.end();
            updatesIcon->setPixmap(pixmap);
            return;
        }
        while (auto *item = updatesRows->takeAt(0)) { delete item->widget(); delete item; }
        // Apps: Flatpaks, and Arch packages that have a menu entry. Everything else is system.
        QList<UpdateEntry> apps, system; QHash<QString, const App *> installedById;
        for (const auto &installed : backend.installed) if (installed.source == "Arch" || installed.source == "AUR / local") installedById.insert(installed.id, &installed);
        for (const auto &entry : pendingUpdates) (entry.source == "Flatpak" || installedById.contains(entry.id) ? apps : system).append(entry);
        int flatpaks = 0; for (const auto &entry : apps) if (entry.source == "Flatpak") ++flatpaks;
        auto group = [this, &installedById](const QString &title, const QList<UpdateEntry> &entries, const QString &name) {
            if (entries.isEmpty()) return;
            updatesRows->addWidget(label(QString("%1 · %2").arg(title).arg(entries.size()), 0, true));
            auto *panel = new QFrame; panel->setObjectName(name);
            panel->setStyleSheet("QFrame { background: palette(alternate-base); border-radius: 10px; } QLabel { background: transparent; }");
            auto *rows = new QVBoxLayout(panel); rows->setContentsMargins(14, 10, 14, 10); rows->setSpacing(4);
            for (const auto &entry : entries) {
                const App *known = entry.source == "Flatpak" ? nullptr : installedById.value(entry.id, nullptr);
                QIcon icon = QIcon::fromTheme(entry.source == "Flatpak" ? entry.id : "package-x-generic", QIcon::fromTheme(entry.source == "Flatpak" ? "application-x-executable" : "package-x-generic"));
                if (entry.source == "Flatpak") { for (const auto &installed : backend.installed) if (installed.id == entry.id && !installed.icon.isNull()) icon = installed.icon; }
                else if (known && !known->icon.isNull()) icon = known->icon;
                auto *row = new QWidget; row->setObjectName("updateRow"); auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 6, 0, 6); layout->setSpacing(12);
                auto *symbol = new QLabel; symbol->setFixedSize(40, 40); symbol->setPixmap(icon.pixmap(40, 40));
                auto *copy = new QVBoxLayout; copy->setSpacing(2);
                auto *title = label(known ? known->name : entry.name, 0, true);
                const auto change = versionDiff(entry.from, entry.to);
                auto *version = label(entry.from.isEmpty() ? "Version " + entry.to : QString()); version->setForegroundRole(QPalette::PlaceholderText);
                if (!entry.from.isEmpty()) { version->setTextFormat(Qt::RichText); version->setText(change.first + "&nbsp;&nbsp;→&nbsp;&nbsp;" + change.second); }
                copy->addWidget(title); copy->addWidget(version);
                layout->addWidget(symbol); layout->addLayout(copy, 1);
                if (entry.source == "Flatpak" || known) {
                    auto *update = new QPushButton(QIcon::fromTheme("system-software-update"), "Update"); update->setMinimumHeight(34); update->setCursor(Qt::PointingHandCursor);
                    if (entry.source == "Flatpak") {
                        update->setObjectName("updateOne");
                        connect(update, &QPushButton::clicked, this, [this, entry] { startJob("Update " + entry.name, "flatpak", {"update", "-y", "--noninteractive", entry.id}); });
                    } else { update->setEnabled(false); update->setToolTip("This package updates with the system update, which arrives in the next version."); }
                    layout->addWidget(update);
                }
                layout->addWidget(new SourceBadge(entry.source));
                rows->addWidget(row);
            }
            updatesRows->addWidget(panel);
        };
        group("Apps", apps, "appUpdates");
        group("System packages", system, "systemUpdates");
        updateAll->setEnabled(flatpaks > 0);
        updateAll->setToolTip(flatpaks > 0 ? QString() : "Only system updates are pending. They arrive in the next version.");
    }

    void openInstalledItem(QListWidgetItem *item) {
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= displayedInstalled.size()) return;
        const auto app = displayedInstalled[index];
        for (const auto &entry : backend.apps)
            for (const auto &package : entry.packages)
                if (package.name == app.id && (package.source == Source::Flatpak) == (app.source == "Flatpak")) { openDetails(entry, app.source == "Flatpak" ? "Flatpak · " + package.repo : QString()); return; }
        // Installed outside the catalogue: the page starts with what the system knows, then fills in from the web.
        CatalogApp local; local.key = "installed:" + app.id; local.name = app.name;
        local.icon = app.icon;
        local.summary = app.summary.isEmpty() ? "Looking for a description…" : app.summary;
        openDetails(local, app.source == "Other" ? QString() : app.source);
        lookUpAbout(app, local);
    }
    // Package database first (offline), then the AUR, then a web summary for apps no package manager knows.
    void lookUpAbout(const App &app, CatalogApp local) {
        auto show = [this, key = local.key, platform = app.source == "Other" ? QString() : app.source](CatalogApp page) { if (appDetails->currentId == key) appDetails->showApp(page, platform); };
        auto fallback = [this, app, local, show](CatalogApp page) mutable {
            if (page.summary == "Looking for a description…") page.summary = "No description found for this app.";
            show(page);
        };
        auto web = [this, app, local, fallback, show]() mutable {
            QUrl url("https://api.duckduckgo.com/"); QUrlQuery query;
            query.addQueryItem("q", app.name); query.addQueryItem("format", "json"); query.addQueryItem("no_html", "1"); query.addQueryItem("skip_disambig", "1");
            url.setQuery(query);
            backend.cachedGet(url, [app, local, fallback, show](QByteArray data) mutable {
                const auto answer = QJsonDocument::fromJson(data).object();
                const QString heading = answer.value("Heading").toString(), text = answer.value("AbstractText").toString();
                // Only accept an answer that is about this name.
                if (!text.isEmpty() && (heading.contains(app.name, Qt::CaseInsensitive) || app.name.contains(heading, Qt::CaseInsensitive))) {
                    local.summary = text.size() > 260 ? text.left(260).section(' ', 0, -2) + "…" : text;
                    if (local.homepage.isEmpty()) local.homepage = answer.value("AbstractURL").toString();
                    show(local);
                } else fallback(local);
            });
        };
        if (app.source == "Arch" || app.source == "AUR / local") {
            backend.readCommand("pacman", {"-Qi", app.id}, [local, web, show](int code, QString output, QString) mutable {
                QString description, url;
                for (const QString &line : output.split('\n')) {
                    if (line.startsWith("Description")) description = line.section(':', 1).trimmed();
                    else if (line.startsWith("URL")) url = line.section(':', 1).trimmed();
                }
                if (code == 0 && !description.isEmpty() && description != "None") { local.summary = description; local.homepage = url; show(local); }
                else web();
            });
        } else if (app.summary.isEmpty()) web();
    }

    void renderInstalled() {
        if (!installedList || !installedSearch || !installedFilter) return;
        displayedInstalled = backend.installed;
        installedList->clear();
        const QString query = installedSearch->text().trimmed(), source = installedFilter->currentText();
        QList<int> matches;
        for (int i = 0; i < displayedInstalled.size(); ++i) {
            const auto &app = displayedInstalled[i];
            if ((source == "All sources" || app.source == source) && (app.name.contains(query, Qt::CaseInsensitive) || app.id.contains(query, Qt::CaseInsensitive))) matches.append(i);
        }
        std::stable_sort(matches.begin(), matches.end(), [this](int a, int b) { return QString::compare(displayedInstalled[a].name, displayedInstalled[b].name, Qt::CaseInsensitive) < 0; });
        installedLabel->setText(QString("Installed apps · A–Z · %1").arg(matches.size()));
        for (int index : matches) {
            const auto &app = displayedInstalled[index];
            QIcon icon = appIcon(app);
            if (app.source == "Flatpak") for (const auto &entry : backend.catalogue) if (entry.id == app.id && !entry.icon.isNull()) { icon = entry.icon; break; }
            QStringList metadata{app.source};
            if (!app.remote.isEmpty()) metadata << app.remote;
            if (!app.scope.isEmpty()) metadata << app.scope;
            if (!app.version.isEmpty()) metadata << app.version;
            // One quiet line: what the app is, or its version when its publisher says nothing.
            const QString line = !app.summary.isEmpty() ? app.summary : app.version.isEmpty() ? QString() : "Version " + app.version;
            const bool managed = app.source != "Other";  // Only apps with a package manager or one of our files can be removed here
            installedList->addApp(app.name, line, icon, index, metadata.join(" · "), managed ? &previewOperations : nullptr, app.id, true, app.source, app.source != "AppImage");
        }
        installedList->fitRows(); installedEmpty->setVisible(matches.isEmpty());
    }

};

static long residentMegabytes() {
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLong() / 1024;
    return -1;
}

// Loads every source headlessly and prints what the catalogue contains.
static int catalogStats(QApplication &application) {
    Backend backend;
    QElapsedTimer clock;
    clock.start();
    QObject::connect(&backend, &Backend::issue, [](const QString &message) { std::fprintf(stderr, "issue: %s\n", qPrintable(message)); });
    QObject::connect(&backend, &Backend::catalogReady, &application, [&] {
        int packages[3] = {}, withMetadata = 0, aurDetailed = 0, aurLibraries = 0, overlay = 0, overlayIcons = 0;
        for (const auto &app : backend.apps) {
            if (app.origin != MetadataOrigin::None) ++withMetadata;
            if (app.origin == MetadataOrigin::Overlay) { ++overlay; if (QFileInfo::exists(app.iconPath)) ++overlayIcons; }
            for (const auto &package : app.packages) ++packages[int(package.source)];
        }
        for (const auto &entry : backend.aur) { if (entry.detailed()) ++aurDetailed; if (isLibraryName(entry.name)) ++aurLibraries; }
        std::printf("Entries:              %lld (with AppStream metadata: %d)\n", qlonglong(backend.apps.size()), withMetadata);
        std::printf("WoofStore metadata:   %d entries, %d with icons\n", overlay, overlayIcons);
        std::printf("Flatpak packages:     %d\n", packages[int(Source::Flatpak)]);
        std::printf("Repo packages:        %d\n", packages[int(Source::Repo)]);
        std::printf("AUR packages:         %lld indexed, %d with details, %d library-like\n", qlonglong(backend.aur.size()), aurDetailed, aurLibraries);
        const auto &m = backend.mergeStats;
        const int merged = m.byId + m.byName + m.byHomepage + m.byOverride;
        std::printf("Merged:               %d (same ID %d, name rules %d, name+homepage %d, overrides %d)\n", merged, m.byId, m.byName, m.byHomepage, m.byOverride);
        std::printf("Multi-source entries: %d\n", m.multiSource);
        std::printf("Name matches merged:  %.1f %% (%d near misses left separate)\n", 100.0 * (m.byName + m.byHomepage + m.byOverride) / qMax(1, m.byName + m.byHomepage + m.byOverride + int(m.nearMisses.size())), int(m.nearMisses.size()));
        if (qEnvironmentVariableIsSet("WOOFSTORE_SHOW_MERGES")) { for (const auto &line : m.nearMisses) std::printf("  near miss: %s\n", qPrintable(line)); for (const auto &line : m.merges) std::printf("  merged %s\n", qPrintable(line)); }
        std::printf("Repo metadata:        %s\n", backend.osMetadataMissing ? "missing (install archlinux-appstream-data)" : "loaded");
        std::printf("Load time:            %lld ms\n", qlonglong(clock.elapsed()));
        std::printf("Memory (VmRSS):       %ld MB\n", residentMegabytes());
        application.quit();
    });
    backend.load();
    return application.exec();
}

// The fixture catalogue entry with this name (tests/fixtures/catalog.json).
static const CatalogApp &fixtureApp(const Backend &backend, const QString &name) {
    for (const auto &app : backend.apps) if (app.name == name) return app;
    qFatal("Fixture app %s is missing", qPrintable(name));
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    // Headless previews have no desktop menu-bar service.
    if (application.platformName() == "offscreen") application.setAttribute(Qt::AA_DontUseNativeMenuBar);
    application.setApplicationName("WoofStore");
    application.setApplicationDisplayName("WoofStore");
    application.setOrganizationName("WoofStore");
    if (geteuid() == 0) { qCritical("Run WoofStore as your normal user. Package managers request elevation when needed."); return 1; }
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("breeze");
    QIcon::setFallbackThemeName("breeze");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native Qt 6 / KDE software store for CachyOS");
    parser.addHelpOption();
    parser.addOption({"screenshot", "Save an offscreen UI preview and exit (does not install software).", "path"});
    parser.addOption({"import-appimage", "Copy an AppImage into the app menu and exit (used by the install window).", "path"});
    parser.addOption({"installed-open", "Open the first Installed app in the main detail view."});
    parser.addOption({"explore-end", "Preview the Load more button at the directory end."});
    parser.addOption({"activity-sidebar", "Preview simulated progress in the sidebar."});
    parser.addOption({"install-preview", "Preview the simulated installing state on app details."});
    parser.addOption({"activity-preview", "Show a simulated activity popup without package commands."});
    parser.addOption({"activity-empty", "Show Activity without any operations."});
    parser.addOption({"prototype-check", "Check simulated action progress and popup without package commands."});
    parser.addOption({"list-selected", "Show the first list selection in a preview."});
    parser.addOption({"updates-preview", "Preview an update state without package commands: idle, checking, failed.", "state"});
    parser.addOption({"installed-preview", "Show a labeled sample Installed layout without package operations."});
    parser.addOption({"page", "Preview page: explore, installed, updates.", "page", "explore"});
    parser.addOption({"compact", "Use a narrow desktop preview."});
    parser.addOption({"expanded", "Show the expanded sidebar in a preview."});
    parser.addOption({"collapsed", "Show the icon-only sidebar in a preview."});
    parser.addOption({"explore-check", "Check the Explore prototype without package operations."});
    parser.addOption({"carousel-slide", "Preview featured slide 0, 1, or 2.", "index", "0"});
    parser.addOption({"mosaic-end", "Preview the final discovery mosaic cards."});
    parser.addOption({"mosaic-hover", "Preview a floating mosaic app card.", "app-id"});
    parser.addOption({"explore-collection", "Preview the discovery collection."});
    parser.addOption({"explore-filters", "Show the expanded Explore filters."});
    parser.addOption({"search", "Type this search in a live-catalog preview.", "text"});
    parser.addOption({"details-app", "Preview an app detail page.", "app-id"});
    parser.addOption({"details-gallery", "Preview the screenshot carousel."});
    parser.addOption({"details-slide", "Preview an app screenshot index.", "index", "0"});
    parser.addOption({"details-changelog", "Show the latest cached release notes."});
    parser.addOption({"details-bottom", "Preview app permissions."});
    parser.addOption({"explore-bottom", "Preview the alphabetical directory."});
    parser.addOption({"sidebar-check", "Check sidebar controls without loading software or changing user settings."});
    parser.addOption({"terminal-demo", "Run a harmless prompt test in the embedded terminal."});
    parser.addOption({"terminal-check", "Automatically test the embedded terminal without system changes."});
    parser.addOption({"live-catalog", "Use the live software sources in a screenshot preview instead of the test catalogue."});
    parser.addOption({"catalog-stats", "Load every software source, print catalogue counts and memory use, then exit."});
    parser.process(application);
    if (parser.isSet("import-appimage")) {
        int failures = 0;
        const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        for (const QString &file : parser.values("import-appimage")) {
            std::printf("==> Copying %s\n", qPrintable(QFileInfo(file).fileName())); std::fflush(stdout);
            QString error;
            if (importAppImage(file, dataRoot, &error).isEmpty()) { std::printf("error: %s\n", qPrintable(error)); ++failures; }
            else std::printf("==> Adding to the app menu\n");
            std::fflush(stdout);
        }
        return failures ? 1 : 0;
    }
    if (parser.isSet("catalog-stats")) return catalogStats(application);
    QPalette p = application.palette();
    for (const auto group : {QPalette::Active, QPalette::Inactive})
        p.setColor(group, QPalette::PlaceholderText, p.color(group, QPalette::Text));
    application.setPalette(p);
    QTemporaryDir sidebarCheckConfig;
    if (parser.isSet("prototype-check") || parser.isSet("sidebar-check") || parser.isSet("explore-check")) qputenv("XDG_CONFIG_HOME", sidebarCheckConfig.path().toUtf8());
    Store store(parser.isSet("live-catalog") || (!parser.isSet("screenshot") && !parser.isSet("prototype-check") && !parser.isSet("activity-preview") && !parser.isSet("updates-preview") && !parser.isSet("installed-preview") && !parser.isSet("terminal-check") && !parser.isSet("sidebar-check") && !parser.isSet("explore-check")));
    store.show();
    if (parser.isSet("prototype-check")) {
        store.previewInstalled();
        auto *page = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
        auto *details = static_cast<AppDetailsPage *>(store.findChild<QWidget *>("appDetailsPage"));
        auto *installed = store.findChild<QListWidget *>("installedApps");
        installed->itemClicked(installed->item(0));
        bool passed = details->currentId == fixtureApp(store.backend, "Firefox").key && details->isVisible()
            && store.findChild<QButtonGroup *>("pageNavigation")->checkedId() == 1 && details->back->text() == "Installed";
        details->back->click(); store.selectPage(1);
        {   // The two cards take only their own file type, and nothing is installed by a check
            QTemporaryDir droppedFiles;
            auto make = [&](const QString &name) { const QString path = droppedFiles.path() + '/' + name; QFile file(path); file.open(QIODevice::WriteOnly); file.write("fixture"); return QUrl::fromLocalFile(path); };
            const QUrl appImage = make("Zzz-preview.AppImage"), flatpakRef = make("Zzz-flatpak.flatpakref");
            auto *appImageCard = static_cast<DropCard *>(store.findChild<QAbstractButton *>("addAppImage")), *flatpakCard = static_cast<DropCard *>(store.findChild<QAbstractButton *>("addFlatpak"));
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            auto offer = [](DropCard *card, const QList<QUrl> &urls) {
                QMimeData mime; mime.setUrls(urls);
                QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(card, &enter);
                const bool taken = enter.isAccepted() && card->dragging;
                QDragLeaveEvent leave; QApplication::sendEvent(card, &leave);
                return taken && !card->dragging;
            };
            QT_WARNING_POP
            passed &= appImageCard && flatpakCard && offer(appImageCard, {appImage}) && offer(flatpakCard, {flatpakRef});
            passed &= !offer(appImageCard, {flatpakRef}) && !offer(flatpakCard, {appImage}) && !offer(appImageCard, {QUrl("https://example.invalid/app.AppImage")});
            passed &= installed->count() == 3 && !store.findChild<QWidget *>("fileDropOverlay");
        }
        store.backend.installedChanged(); passed &= installed->count() == 3;
        passed &= page->loadMore->isHidden();  // The fixture fits in one page
        store.previewUpdates("available");
        passed &= !store.findChild<QWidget *>("updateResults")->isHidden() && store.findChild<QWidget *>("updatesEmpty")->isHidden()
            && store.findChildren<QPushButton *>("updateOne").size() == 2 && store.findChild<QPushButton *>("updateAll")->isVisible() && store.findChild<QFrame *>("systemUpdates");
        store.previewUpdates("idle");
        passed &= store.findChild<QWidget *>("updateResults")->isHidden() && store.findChild<QLabel *>("updatesTitle")->text() == "You're all up to date";
        CatalogApp longApp; longApp.key = "test-long-description"; longApp.name = "Long description"; longApp.origin = MetadataOrigin::AppStream;
        for (int line = 1; line <= 12; ++line) longApp.description += QString("<p>Paragraph %1 of the publisher description.</p>").arg(line);
        page->openPreview(longApp);
        auto *description = details->findChild<QLabel *>("detailsDescription");
        auto *descriptionToggle = details->findChild<QToolButton *>("descriptionToggle");
        const int collapsedDescriptionHeight = description->height();
        passed &= !descriptionToggle->isHidden() && descriptionToggle->text() == "Show more"
            && collapsedDescriptionHeight <= QFontMetrics(description->font()).lineSpacing() * 6;
        const QString publisherCopy = description->text();
        descriptionToggle->click();
        details->findChild<QVariantAnimation *>("descriptionMotion")->setCurrentTime(details->findChild<QVariantAnimation *>("descriptionMotion")->duration());
        passed &= description->height() > collapsedDescriptionHeight && descriptionToggle->text() == "Show less"
            && description->text() == publisherCopy;
        descriptionToggle->click(); details->findChild<QVariantAnimation *>("descriptionMotion")->setCurrentTime(details->findChild<QVariantAnimation *>("descriptionMotion")->duration()); passed &= description->height() == collapsedDescriptionHeight;
        CatalogApp shortApp; shortApp.key = "test-short-description"; shortApp.name = "Short description"; shortApp.summary = "One short line.";
        page->openPreview(shortApp);
        passed &= descriptionToggle->isHidden() && description->text() == "One short line.";
        for (const QString &name : {QString("changelogPanel"), QString("addonPanel"), QString("permissionPanel"), QString("linksPanel")})
            passed &= details->findChild<QWidget *>(name)->isHidden();
        passed &= details->screenshot->isHidden();
        page->openPreview(fixtureApp(store.backend, "Krita")); details->install->click();
        // Permissions come only from an installed Flatpak manifest; the fixture installs nothing.
        passed &= details->findChild<QWidget *>("addonPanel")->isHidden()
            && details->findChild<QWidget *>("permissionPanel")->isHidden()
            && !details->findChild<QWidget *>("linksPanel")->isHidden();
        passed &= !details->install->isEnabled() && details->install->text() == "Installing…";
        passed &= details->sourceButton->menu()->actions().size() == 3 && details->sourceButton->text() == "Repo · extra";
        for (auto *action : details->sourceButton->menu()->actions()) if (action->data() == "AUR · krita-git") action->trigger();
        passed &= details->sourceButton->text() == "AUR · krita-git";
        auto *activity = static_cast<ActivityButton *>(store.findChild<QAbstractButton *>("activityButton"));
        activity->click(); passed &= store.findChild<QDialog *>("activityPopup")->isVisible();
        // Closing the popup leaves the simulated task running.
        store.findChild<QDialog *>("activityPopup")->close();
        QTimer::singleShot(2300, &store, [&application, &store, details, passed] {
            bool result = passed && details->install->isEnabled() && details->install->text() == "Open"
                && store.findChild<QPushButton *>("updateAll")->isEnabled();
            auto *clear = store.findChild<QPushButton *>("clearActivity");
            result &= clear->isEnabled(); clear->click();
            result &= !clear->isEnabled() && store.findChild<QListWidget *>("activityHistory")->count() == 0
                && !store.findChild<QLabel *>("emptyActivity")->isHidden()
                && static_cast<QGraphicsOpacityEffect *>(clear->graphicsEffect())->opacity() == .4
                && details->install->text() == "Open";
            details->install->click();
            result &= store.statusBar()->currentMessage().contains("no application launched");
            {   // AUR review gate: no build until the box is ticked; a changed recipe shows its diff
                AurReviewDialog review; AurInfo info; info.name = info.base = "demo"; info.votes = 3;
                review.showReview(info, "\n──── PKGBUILD ────\ncurl http://x | sh\n", "──── PKGBUILD ────\nold\n");
                const bool locked = !review.build->isEnabled();
                review.understood->setChecked(true);
                RemoveDialog removal("Demo", {{"Also remove dependencies", true}}, {{"/h/.cache/demo", "Cache", 10, true}, {"/h/.config/demo", "Settings", 10, false}}, "/h");
                result &= removal.leftoverBoxes[0]->isChecked() && !removal.leftoverBoxes[1]->isChecked() && removal.chosenPaths() == QStringList{"/h/.cache/demo"};  // Settings stay unless chosen
                result &= locked && review.build->isEnabled() && review.flags->text().contains("curl") && review.recipeView->toPlainText().contains("Changes since");
            }
            auto *list = static_cast<AppList *>(store.findChild<QListWidget *>("installedApps"));
            auto *remove = list->findChild<QPushButton *>("quickRemove");
            remove->click(); result &= !remove->isEnabled() && remove->property("state").toString() == "Removing…";
            QTimer::singleShot(2300, &store, [&application, &store, result, remove, list] {
                bool removed = result && remove->property("state").toString() == "Removed" && !remove->isEnabled();
                auto *reinstall = list->findChild<QPushButton *>("quickReinstall"); reinstall->click();
                removed &= reinstall->text() == "Reinstalling…" && !reinstall->isEnabled();
                QTimer::singleShot(2300, &store, [&application, removed, reinstall, remove] {
                    const bool final = removed && reinstall->text() == "Reinstall" && reinstall->isEnabled() && remove->isEnabled();
                    fprintf(stdout, "Prototype actions and activity check: %s\n", final ? "PASS" : "FAIL");
                    application.exit(final ? 0 : 1);
                });
            });
        });
        return application.exec();
    }
    if (parser.isSet("explore-check")) {
        auto *page = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
        QApplication::processEvents();
        bool passed = page->check();
        auto *details = static_cast<AppDetailsPage *>(store.findChild<QWidget *>("appDetailsPage"));
        passed &= details->check() && !details->isHidden() && !details->back->hasFocus();
        details->back->click();
        passed &= !page->isHidden();
        page->openPreview(fixtureApp(store.backend, "GIMP"));
        passed &= !details->findChild<QWidget *>("scorePanel") && details->findChild<QLabel *>("releaseNotes")->text().contains("GIMP")
            && !details->findChild<QLabel *>("releaseVersion")->isHidden();
        auto *addonPanel = details->findChild<QWidget *>("addonPanel");
        auto *permissionPanel = details->findChild<QWidget *>("permissionPanel");
        auto *linksPanel = details->findChild<QWidget *>("linksPanel");
        QApplication::processEvents();
        passed &= details->findChild<QWidget *>("changelogPanel")->y() < addonPanel->y()
            && addonPanel->y() < linksPanel->y() && permissionPanel->isHidden()
            && !linksPanel->findChildren<QCommandLinkButton *>("appLink").isEmpty();
        auto *addonInstall = addonPanel->findChild<QPushButton *>("installAddon");
        auto *addonOpen = addonPanel->findChild<QCommandLinkButton *>("openAddon");
        passed &= addonInstall && addonOpen;
        if (addonInstall && addonOpen) {
            addonInstall->click(); passed &= addonInstall->text() == "Installing…" && details->currentId == fixtureApp(store.backend, "GIMP").key;
            const QString addonName = addonOpen->text(); addonOpen->click();
            passed &= details->title->text() == addonName && details->currentId != fixtureApp(store.backend, "GIMP").key && details->check();
            details->back->click(); passed &= details->currentId == fixtureApp(store.backend, "GIMP").key;
        }
        details->back->click();
        page->openPreview(fixtureApp(store.backend, "Firefox"));
        passed &= details->findChild<QWidget *>("changelogPanel")->isHidden()
            && !details->back->autoRaise() && !details->findChild<QToolButton *>("descriptionToggle")->autoRaise();
        for (auto *label : details->findChildren<QLabel *>()) passed &= !label->text().contains("Preview");
        details->back->click();
        auto *featured = page->carousel;
        auto *featuredInstall = static_cast<ExploreTile *>(featured->widget(1))->quickInstall;
        featuredInstall->click();
        passed &= featuredInstall->text() == "Installing…" && page->isVisible() && details->isHidden();
        auto *motion = featured->findChild<QVariantAnimation *>("featuredAnimation");
        auto *transition = featured->findChild<QLabel *>("featuredTransition");
        page->advance(1);
        passed &= motion->state() == QAbstractAnimation::Running && !transition->isHidden();
        const QImage firstFrame = transition->pixmap().toImage();
        motion->setCurrentTime(motion->duration() / 2);
        passed &= transition->pixmap().toImage() != firstFrame;
        page->advance(1); page->advance(1);
        passed &= featured->currentIndex() == 1 && motion->state() == QAbstractAnimation::Running;
        motion->setCurrentTime(motion->duration());
        featured->select(featured->count() - 1); motion->setCurrentTime(motion->duration());
        featured->select(0); passed &= motion->endValue().toReal() == -1.;
        motion->setCurrentTime(motion->duration());
        featured->select(featured->count() - 1); passed &= motion->endValue().toReal() == 1.;
        motion->setCurrentTime(motion->duration());
        featured->select(1); motion->setCurrentTime(motion->duration());
        passed &= transition->isHidden() && motion->state() == QAbstractAnimation::Stopped;
        page->advance(1); featured->hide();
        passed &= transition->isHidden() && motion->state() == QAbstractAnimation::Stopped;
        featured->show();
        QSettings motionSettings(sidebarCheckConfig.path() + "/kdeglobals", QSettings::IniFormat);
        motionSettings.setValue("KDE/AnimationDurationFactor", 0.); motionSettings.sync();
        page->advance(1);
        passed &= featured->currentIndex() == 0 && transition->isHidden() && motion->state() == QAbstractAnimation::Stopped;
        motionSettings.setValue("KDE/AnimationDurationFactor", 1.); motionSettings.sync();
        auto *autoplay = page->findChild<QTimer *>("featuredAutoplay");
        QApplication::processEvents(); passed &= autoplay->isActive() && autoplay->interval() == 5000;
        QEnterEvent hover(QPointF(20, 20), QPointF(20, 20), QPointF(20, 20));
        QApplication::sendEvent(featured, &hover); QApplication::processEvents(); passed &= !autoplay->isActive();
        QEvent leave(QEvent::Leave); QApplication::sendEvent(featured, &leave); QApplication::processEvents();
        passed &= autoplay->isActive();
        auto *featuredDot = static_cast<CarouselDot *>(page->findChild<QAbstractButton *>("featuredDot0"));
        // Mouse focus must not keep autoplay paused after leaving the carousel.
        featuredDot->setFocus(Qt::MouseFocusReason); QApplication::processEvents(); passed &= autoplay->isActive();
        featuredDot->clearFocus(); featuredDot->setFocus(Qt::TabFocusReason); QApplication::processEvents(); passed &= !autoplay->isActive();
        featuredDot->clearFocus(); QApplication::processEvents(); passed &= autoplay->isActive();
        const int beforeAutoplay = featured->currentIndex();
        QEventLoop rotationWait;
        QObject::connect(autoplay, &QTimer::timeout, &rotationWait, &QEventLoop::quit);
        QTimer::singleShot(6000, &rotationWait, &QEventLoop::quit); rotationWait.exec();
        passed &= featured->currentIndex() == (beforeAutoplay + 1) % featured->count();
        const auto indicators = page->findChildren<QVariantAnimation *>("indicatorAnimation");
        bool movingIndicator = false;
        for (auto *barMotion : indicators) if (barMotion->state() == QAbstractAnimation::Running) {
            movingIndicator = true; barMotion->setCurrentTime(barMotion->duration() / 2);
            passed &= barMotion->currentValue().toReal() != barMotion->startValue().toReal();
        }
        passed &= movingIndicator;
        for (auto *barMotion : indicators) {
            if (barMotion->state() == QAbstractAnimation::Running) barMotion->setCurrentTime(barMotion->duration());
            auto *bar = qobject_cast<QAbstractButton *>(barMotion->parent());
            passed &= bar && bar->width() == (bar->isChecked() ? 84 : 28);
        }
        const int sourceScroll = page->verticalScrollBar()->value();
        auto wheelFeatured = [](QWidget *target, const QPoint &angle, const QPoint &pixels = QPoint()) {
            const QPointF position = target->rect().center();
            QWheelEvent event(position, target->mapToGlobal(position.toPoint()), pixels, angle, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &event);
        };
        int beforeWheel = featured->currentIndex();
        wheelFeatured(featured->currentWidget(), QPoint(0, -120));
        passed &= featured->currentIndex() == (beforeWheel + 1) % featured->count();
        wheelFeatured(featuredDot, QPoint(0, 120)); passed &= featured->currentIndex() == beforeWheel;
        auto *hero = static_cast<ExploreTile *>(featured->currentWidget());
        wheelFeatured(hero->quickInstall, QPoint(0, -120));
        passed &= featured->currentIndex() == (beforeWheel + 1) % featured->count();
        beforeWheel = featured->currentIndex();
        wheelFeatured(featured, QPoint(), QPoint(-30, 0)); passed &= featured->currentIndex() == beforeWheel;
        wheelFeatured(featured, QPoint(), QPoint(-30, 0));
        passed &= featured->currentIndex() == (beforeWheel + 1) % featured->count()
            && page->verticalScrollBar()->value() == sourceScroll;
        page->search->setText("firefox"); page->applyFilters(); QApplication::processEvents(); passed &= !autoplay->isActive();
        page->search->clear(); page->applyFilters(); QApplication::processEvents(); passed &= autoplay->isActive();
        // The fixture page is short: a small window makes the carousel scroll out of view.
        const QSize fullSize = store.size(); const QSize minimum = store.minimumSize(); store.setMinimumHeight(200); store.resize(fullSize.width(), 240); QApplication::processEvents();
        page->showDirectory(); QApplication::processEvents(); passed &= !autoplay->isActive();
        page->verticalScrollBar()->setValue(0); QApplication::processEvents(); passed &= autoplay->isActive();
        store.setMinimumSize(minimum); store.resize(fullSize); QApplication::processEvents();
        // GIMP: the featured install above already finished Krita, so Krita's quick button reads "Open".
        auto *tile = page->sections.first().masonry->tiles[0]; page->ensureWidgetVisible(tile); QApplication::processEvents();
        QList<QRect> mosaicGeometry; for (auto *card : page->sectionTiles()) mosaicGeometry.append(card->geometry());
        QApplication::sendEvent(tile, &hover);
        auto *hoverDelay = page->findChild<QTimer *>("mosaicHoverDelay");
        passed &= hoverDelay->isActive(); QMetaObject::invokeMethod(hoverDelay, "timeout", Qt::DirectConnection);
        auto *preview = static_cast<ExploreTile *>(page->findChild<QAbstractButton *>("mosaicHoverPreview"));
        const QRect sourceRect(tile->mapTo(page->viewport(), QPoint()), tile->size());
        auto *hoverMotion = page->findChild<QVariantAnimation *>("mosaicHoverMotion");
        passed &= preview->geometry() == sourceRect;
        hoverMotion->setCurrentTime(hoverMotion->duration());
        preview->quickInstall->click();
        passed &= preview->quickInstall->text() == "Installing…" && page->isVisible() && details->isHidden()
            && preview->rect().contains(preview->quickInstall->geometry());
        passed &= preview->geometry().contains(sourceRect.center()) && preview->width() >= qMin(tile->width() + 48, page->viewport()->width() - 24)
            && preview->height() >= qMin(tile->height() + 24, page->viewport()->height() - 24);
        passed &= preview->isVisible() && preview->app.key == tile->app.key && !preview->artwork.isNull()
            && page->viewport()->rect().contains(preview->geometry());
        for (int i = 0; i < mosaicGeometry.size(); ++i) passed &= mosaicGeometry[i] == page->sectionTiles()[i]->geometry();
        auto *dismiss = page->findChild<QTimer *>("mosaicDismissDelay");
        auto *fade = page->findChild<QVariantAnimation *>("mosaicHoverFade");
        auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(preview->graphicsEffect());
        QApplication::sendEvent(preview, &leave); QMetaObject::invokeMethod(dismiss, "timeout", Qt::DirectConnection);
        fade->setCurrentTime(fade->duration() / 2);
        passed &= preview->isVisible() && opacity->opacity() > 0. && opacity->opacity() < 1.;
        QApplication::sendEvent(preview, &hover);
        passed &= fade->state() == QAbstractAnimation::Stopped && opacity->opacity() == 1.;
        QApplication::sendEvent(preview, &leave); QMetaObject::invokeMethod(dismiss, "timeout", Qt::DirectConnection);
        fade->setCurrentTime(fade->duration()); passed &= preview->isHidden();
        QApplication::sendEvent(tile, &hover); QMetaObject::invokeMethod(hoverDelay, "timeout", Qt::DirectConnection);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QApplication::sendEvent(tile, &escape);
        passed &= preview->isHidden();
        QApplication::sendEvent(tile, &hover); QMetaObject::invokeMethod(hoverDelay, "timeout", Qt::DirectConnection);
        preview->click(); passed &= details->currentId == tile->app.key && !details->isHidden(); details->back->click();
        page->verticalScrollBar()->setValue(0); QApplication::processEvents();
        for (const auto &app : store.backend.apps) {
            page->openPreview(app); QApplication::processEvents();
            passed &= details->check() && details->currentId == app.key && !details->isHidden();
            details->back->click(); passed &= !page->isHidden();
        }
        page->search->setText("firefox"); page->applyFilters();
        const int previousScroll = page->verticalScrollBar()->value();
        page->openPreview(fixtureApp(store.backend, "Firefox"));
        details->back->click();
        passed &= page->search->text() == "firefox" && page->results->count() == 1
            && page->verticalScrollBar()->value() == previousScroll;
        page->search->clear(); page->applyFilters();
        page->openPreview(fixtureApp(store.backend, "Krita")); QApplication::processEvents();
        auto *gallery = details->screenshot;
        passed &= gallery->images.size() >= 3;
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(gallery, &right); passed &= gallery->current == 1;
        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(gallery, &left); passed &= gallery->current == 0;
        QApplication::sendEvent(gallery, &left); passed &= gallery->current == gallery->images.size() - 1;
        gallery->select(0, false);
        const auto detailIndicators = details->findChildren<QVariantAnimation *>("indicatorAnimation");
        passed &= !detailIndicators.isEmpty();
        for (auto *motion : detailIndicators) passed &= static_cast<CarouselDot *>(motion->parent())->playful;
        gallery->select(1);
        bool animatingDetailIndicator = false;
        for (auto *motion : details->findChildren<QVariantAnimation *>("indicatorAnimation")) {
            if (motion->state() == QAbstractAnimation::Running) {
                animatingDetailIndicator = true; motion->setCurrentTime(motion->duration());
            }
            auto *dot = qobject_cast<QAbstractButton *>(motion->parent());
            passed &= dot && dot->width() == (dot->isChecked() ? 84 : 28);
        }
        passed &= animatingDetailIndicator;
        gallery->select(0, false);
        details->showGallery(); QApplication::processEvents();
        const QPointF wheelPosition = gallery->rect().center();
        const QPointF wheelGlobal = gallery->mapToGlobal(wheelPosition.toPoint());
        const int pageScroll = details->verticalScrollBar()->value();
        QWheelEvent wheel(store.mapFromGlobal(wheelGlobal.toPoint()), wheelGlobal, QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(gallery, &wheel);
        fprintf(stdout, "Wheel routing: screenshot=%d, page=%d -> %d\n", gallery->current, pageScroll, details->verticalScrollBar()->value());
        passed &= gallery->current == 0 && details->verticalScrollBar()->value() > pageScroll;
        QWheelEvent trackpad(store.mapFromGlobal(wheelGlobal.toPoint()), wheelGlobal, QPoint(0, -80), QPoint(), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(gallery, &trackpad); passed &= gallery->current == 0;
        gallery->select(0, false);
        QMouseEvent dragStart(QEvent::MouseButtonPress, QPointF(160, 80), QPointF(160, 80), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent dragEnd(QEvent::MouseButtonRelease, QPointF(80, 80), QPointF(80, 80), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(gallery, &dragStart); QApplication::sendEvent(gallery, &dragEnd);
        passed &= gallery->current == 1;
        auto *nav = store.findChild<QButtonGroup *>("pageNavigation");
        auto settle = [] {
            QEventLoop loop; QTimer::singleShot(30, &loop, &QEventLoop::quit); loop.exec();
        };
        auto clickNavigation = [nav, settle](int row) {
            const QPoint point = nav->button(row)->rect().center();
            const QPoint global = nav->button(row)->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(nav->button(row), &press); QApplication::sendEvent(nav->button(row), &release);
            settle();
        };
        for (int width : {720, 760, 1180}) {
            store.resize(width, 800); settle();
            for (bool compact : {false, true}) {
                store.findChild<QAction *>("compactSidebar")->setChecked(compact); store.findChild<QVariantAnimation *>("sidebarMotion")->setCurrentTime(store.findChild<QVariantAnimation *>("sidebarMotion")->duration()); settle();
                for (int destination : {1, 2}) {
                    clickNavigation(destination);
                    passed &= page->isHidden() && nav->checkedId() == destination;
                    store.backend.catalogueChanged(); store.backend.installedChanged(); settle();
                    clickNavigation(0);
                    passed &= page->isVisible() && nav->checkedId() == 0 && page->results->count() == 3
                        && page->sections.first().masonry->height() > 1 && page->search->isVisible();
                    for (const auto &section : page->sections) {
                        if (section.masonry->isHidden()) continue;  // Other Flathub tabs
                        const auto tiles = section.masonry->tiles;
                        QRegion uncovered(section.masonry->rect());
                        for (int i = 0; i < tiles.size(); ++i) {
                            uncovered -= tiles[i]->geometry().adjusted(0, 0, 12, 12);
                            if (tiles[i]->spotlight) passed &= !tiles[i]->artwork.isNull() && tiles.size() >= 3;
                            passed &= section.masonry->rect().contains(tiles[i]->geometry())
                                && tiles[i]->height() >= tiles[i]->heightForWidth(tiles[i]->width());
                            for (int j = i + 1; j < tiles.size(); ++j) passed &= !tiles[i]->geometry().intersects(tiles[j]->geometry());
                        }
                    }
                    for (int i = 0; i < page->results->count(); ++i) {
                        auto *row = page->results->itemWidget(page->results->item(i));
                        auto *action = row->findChild<QPushButton *>("quickInstall");
                        passed &= row->width() <= page->results->viewport()->width()
                            && action->geometry().right() < row->width() - 7;
                    }
                }
            }
        }
        page->openPreview(fixtureApp(store.backend, "Krita"));
        nav->button(0)->click(); settle();
        passed &= page->isVisible();
        page->openPreview(fixtureApp(store.backend, "Krita"));
        store.selectPage(0); settle();
        {   // A background job reports steps and output without a terminal
            store.startJob("Job test", "bash", {"-c", "echo '==> Making package'; echo 'Installing 50%'; exit 0"}, {}, {"A", "B", "C"}, [](const QString &text) { return text.contains("Making package") ? 2 : 0; });
            auto *jobView = static_cast<JobDialog *>(store.findChild<QDialog *>("jobDialog"));
            QElapsedTimer waited; waited.start();
            while (jobView && jobView->close->text() != "Close" && waited.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 50);
            passed &= jobView && jobView->close->text() == "Close" && jobView->log->toPlainText().contains("Making package") && jobView->step == 3 && jobView->track->filled();
            passed &= !store.packageJobs.isEmpty() && store.packageJobs.last().finished && store.packageJobs.last().ok && store.packageJobs.last().log.contains("Making package");
            QElapsedTimer closing; closing.start();
            while (store.findChild<QDialog *>("jobDialog") && closing.elapsed() < 4000) QApplication::processEvents(QEventLoop::AllEvents, 50);
            store.openJob(store.packageJobs.size() - 1);  // From the history the finished window comes back with its log
            auto *again = static_cast<JobDialog *>(store.findChild<QDialog *>("jobDialog"));
            passed &= again && again->isVisible() && again->log->toPlainText().contains("Making package") && again->track->filled();
            if (again) again->QDialog::close();
        }
        {   // Typing on Explore goes to the search box
            page->search->clear(); QKeyEvent typed(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, "k");
            QApplication::sendEvent(page->viewport(), &typed);
            QKeyEvent shortcut(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier, "k");
            QApplication::sendEvent(page->viewport(), &shortcut);
            passed &= page->search->text() == "k"; page->search->clear();
        }
        passed &= page->isVisible();
        store.previewInstalled(); settle();
        auto *installed = static_cast<AppList *>(store.findChild<QListWidget *>("installedApps"));
        auto *search = store.findChild<QLineEdit *>("installedSearch");
        auto *source = store.findChild<QComboBox *>("installedFilter");
        passed &= installed->count() == 3 && installed->item(0)->text().startsWith("Firefox\n");
        for (int i = 1; i < installed->count(); ++i)
            passed &= QString::compare(installed->item(i - 1)->text(), installed->item(i)->text(), Qt::CaseInsensitive) <= 0;
        source->setCurrentText("AppImage"); passed &= installed->count() == 0 && installed->isHidden();
        source->setCurrentIndex(0); search->setText("firefox"); passed &= installed->count() == 1;
        passed &= store.backend.installed[installed->item(0)->data(Qt::UserRole).toInt()].name == "Firefox";
        search->clear(); settle();
        passed &= installed->visualItemRect(installed->item(installed->count() - 1)).bottom() < installed->viewport()->height();
        auto mouseHistory = [&store](Qt::MouseButton button) {
            auto *target = store.centralWidget(); const QPointF point(10, 10);
            QMouseEvent press(QEvent::MouseButtonPress, point, target->mapToGlobal(point.toPoint()), button, button, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, point, target->mapToGlobal(point.toPoint()), button, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press); QApplication::sendEvent(target, &release); QApplication::processEvents();
        };
        store.selectPage(0); settle(); page->showCollection(); settle();
        const int exploreScroll = page->verticalScrollBar()->value();
        page->openPreview(fixtureApp(store.backend, "GIMP")); mouseHistory(Qt::BackButton);
        passed &= page->isVisible() && page->verticalScrollBar()->value() == exploreScroll && nav->checkedId() == 0;
        mouseHistory(Qt::ForwardButton);
        passed &= details->isVisible() && details->currentId == fixtureApp(store.backend, "GIMP").key && !details->back->hasFocus();
        mouseHistory(Qt::BackButton); page->openPreview(fixtureApp(store.backend, "Firefox")); mouseHistory(Qt::ForwardButton);
        passed &= details->currentId == fixtureApp(store.backend, "Firefox").key;
        mouseHistory(Qt::BackButton); store.selectPage(1); settle();
        const auto inventory = store.backend.installed;
        for (int copy = 0; copy < 4; ++copy) store.backend.installed.append(inventory);
        store.backend.installedChanged(); settle();
        auto *installedPage = store.findChild<QScrollArea *>("installedPage");
        installedPage->verticalScrollBar()->setValue(qMin(180, installedPage->verticalScrollBar()->maximum()));
        const int installedScroll = installedPage->verticalScrollBar()->value();
        const QString installedId = store.backend.installed[installed->item(12)->data(Qt::UserRole).toInt()].id.toLower();
        installed->itemClicked(installed->item(12));
        passed &= details->back->text() == "Installed" && nav->checkedId() == 1;
        mouseHistory(Qt::BackButton);
        passed &= installedPage->isVisible() && installedScroll > 0 && installedPage->verticalScrollBar()->value() == installedScroll && nav->checkedId() == 1;
        mouseHistory(Qt::ForwardButton); passed &= details->currentId == installedId && details->isVisible();
        details->back->click(); passed &= installedPage->isVisible() && installedPage->verticalScrollBar()->value() == installedScroll;
        store.backend.installed = inventory; store.backend.installedChanged();
        store.previewUpdates("checking");
        passed &= !store.findChild<QPushButton *>("checkUpdates")->isEnabled() && store.findChild<QLineEdit *>("updateStatus")->text() == "Checking…";
        store.previewUpdates("failed");
        passed &= store.findChild<QPushButton *>("checkUpdates")->isEnabled() && store.findChild<QLabel *>("updatesTitle")->text() == "Couldn't check for updates";
        fprintf(stdout, "Explore, Installed, and Updates layout check: %s\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }
    static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"))->advance(qBound(0, parser.value("carousel-slide").toInt(), 2));
    if (parser.isSet("explore-filters")) static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"))->filters->setChecked(true);
    if (parser.isSet("compact")) store.resize(760, 800);
    if (parser.isSet("details-app")) {
        auto *explore = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
        for (const auto &app : store.backend.apps) if (app.key == parser.value("details-app").toLower()) explore->openPreview(app);
    }
    if (parser.isSet("collapsed")) store.findChild<QAction *>("compactSidebar")->setChecked(true);
    if (parser.isSet("expanded")) store.findChild<QAction *>("compactSidebar")->setChecked(false);
    if (parser.isSet("sidebar-check")) {
        store.selectPage(2);
        auto *toggle = store.findChild<QToolButton *>("sidebarToggle");
        auto *side = store.findChild<QWidget *>("sidebar");
        auto *nav = store.findChild<QButtonGroup *>("pageNavigation");
        toggle->click(); store.findChild<QVariantAnimation *>("sidebarMotion")->setCurrentTime(store.findChild<QVariantAnimation *>("sidebarMotion")->duration());
        bool passed = store.findChild<QAbstractButton *>("activityButton")->height() == 48 && side->width() == 56 && store.findChild<QFrame *>("sidebarDivider")->isHidden() && nav->checkedId() == 2 && !store.findChild<QToolButton *>("settingsButton");
        for (auto *button : nav->buttons()) {
            passed &= button->height() == 40 && !button->accessibleName().isEmpty();
        }
        toggle->click(); store.findChild<QVariantAnimation *>("sidebarMotion")->setCurrentTime(store.findChild<QVariantAnimation *>("sidebarMotion")->duration());
        passed &= store.findChild<QAbstractButton *>("activityButton")->height() == 48 && side->width() == 156 && !store.findChild<QFrame *>("sidebarDivider")->isHidden() && nav->checkedId() == 2;
        toggle->click(); store.findChild<QVariantAnimation *>("sidebarMotion")->setCurrentTime(store.findChild<QVariantAnimation *>("sidebarMotion")->duration());
        Store restored(false);
        passed &= restored.findChild<QWidget *>("sidebar")->width() == 56;
        fprintf(stdout, "Sidebar check: %s\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }
    if (parser.value("page") == "installed") store.selectPage(1);
    if (parser.isSet("installed-preview")) store.previewInstalled();
    if (parser.value("page") == "updates") store.selectPage(2);
    if (parser.isSet("updates-preview")) store.previewUpdates(parser.value("updates-preview"));
    if (parser.isSet("installed-open")) {
        auto *list = store.findChild<QListWidget *>("installedApps");
        if (list->count()) {
            auto *item = list->item(0);
            list->itemClicked(item);
        }
    }
    if (parser.isSet("install-preview")) {
        if (parser.isSet("explore-bottom")) store.findChild<QListWidget *>("exploreResults")->findChild<QPushButton *>("quickInstall")->click();
        else store.findChild<QPushButton *>("previewInstall")->click();
    }
    if (parser.isSet("activity-preview") || parser.isSet("activity-sidebar")) store.previewActivity();
    if (parser.isSet("activity-empty")) store.showActivity();
    if (parser.isSet("activity-sidebar")) store.findChild<QDialog *>("activityPopup")->hide();
    QTemporaryDir terminalProof;
    if (parser.isSet("terminal-demo"))
        store.startActivity("Embedded prompt test · no system changes", "bash", {"-c", "printf 'This test does not install or update software.\\nType a response and press Enter: '; IFS= read -r answer; printf '\\nResponse received.\\nPassword test (hidden input): '; IFS= read -r -s secret; printf '\\nHidden input received.\\n'; exit 0"});
    if (parser.isSet("terminal-check")) {
        const QString marker = terminalProof.path() + "/prompt-check-passed";
        store.startActivity("Embedded prompt test · no system changes", "bash", {"-c", "printf 'Response: '; IFS= read -r answer; [ \"$answer\" = hello ] || exit 2; printf 'Hidden input: '; IFS= read -r -s secret; [ \"$secret\" = example ] || exit 3; printf '\\nBoth prompts passed.\\n'; : > \"$1\"", "terminal-check", marker});
        QTimer::singleShot(1000, &store, [&store] {
            for (auto *object : store.findChildren<KParts::ReadOnlyPart *>())
                if (auto *terminal = qobject_cast<TerminalInterface *>(object)) terminal->sendInput("hello\n");
        });
        QTimer::singleShot(1600, &store, [&store] {
            for (auto *object : store.findChildren<KParts::ReadOnlyPart *>())
                if (auto *terminal = qobject_cast<TerminalInterface *>(object)) terminal->sendInput("example\n");
        });
        QTimer::singleShot(2200, &store, [&store] {
            for (auto *object : store.findChildren<KParts::ReadOnlyPart *>())
                if (auto *terminal = qobject_cast<TerminalInterface *>(object)) terminal->sendInput("\n");
        });
        QTimer::singleShot(3500, &store, [&store, &application, marker] {
            const bool passed = store.findChildren<KParts::ReadOnlyPart *>().isEmpty() && QFileInfo::exists(marker);
            fprintf(stdout, "Embedded terminal prompt check: %s\n", passed ? "PASS" : "FAIL");
            application.exit(passed ? 0 : 1);
        });
    }
    if (parser.isSet("screenshot")) {
        auto *timeout = new QTimer(&store);
        timeout->setSingleShot(true);
        auto savePreview = [&store, &parser] {
            return ((parser.isSet("activity-preview") || parser.isSet("activity-empty")) ? store.findChild<QDialog *>("activityPopup")->grab() : store.grab()).save(parser.value("screenshot"));
        };
        auto capture = [&application, timeout, savePreview] {
            if (!timeout->isActive()) return;
            timeout->stop();
            const bool saved = savePreview();
            application.exit(saved ? 0 : 1);
        };
        QObject::connect(timeout, &QTimer::timeout, &store, [&application, savePreview] { application.exit(savePreview() ? 0 : 1); });
        timeout->start(15000);
        if (parser.isSet("live-catalog")) {
            // Live sources: wait for the catalogue, then give screenshots time to download.
            timeout->start(60000);
            QObject::connect(&store.backend, &Backend::catalogReady, &store, [&store, &parser, capture] {
                if (parser.isSet("details-app")) {
                    auto *explore = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
                    for (const auto &app : store.backend.apps) if (app.key == parser.value("details-app").toLower()) { explore->openPreview(app); break; }
                }
                if (parser.isSet("explore-filters")) static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"))->filters->setChecked(true);
                QTimer::singleShot(4000, &store, [&store, &parser] {
                    auto *explore = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
                    if (!parser.value("search").isEmpty()) { explore->search->setText(parser.value("search")); explore->applyFilters(); }
                    if (parser.isSet("explore-collection")) explore->showCollection();
                    if (parser.isSet("explore-bottom")) explore->showDirectory();
                });
                QTimer::singleShot(5000, &store, capture);
            });
        } else if (parser.value("page") == "explore" || parser.isSet("installed-preview") || parser.isSet("updates-preview")) {
            QTimer::singleShot(500, &store, [&store, &parser] {
                auto *details = static_cast<AppDetailsPage *>(store.findChild<QWidget *>("appDetailsPage"));
                if (parser.isSet("details-gallery")) details->showGallery();
                if (parser.isSet("details-slide")) details->screenshot->select(parser.value("details-slide").toInt(), false);
                if (parser.isSet("details-changelog")) details->showChangelog();
                if (parser.isSet("details-bottom")) static_cast<AppDetailsPage *>(store.findChild<QWidget *>("appDetailsPage"))->scrollToPermissions();
                if (parser.isSet("mosaic-hover")) {
                    auto *explore = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
                    for (auto *tile : explore->sectionTiles()) if (tile->app.key == parser.value("mosaic-hover").toLower()) {
                        explore->ensureWidgetVisible(tile); QEnterEvent enter(QPointF(10, 10), QPointF(10, 10), QPointF(10, 10)); QApplication::sendEvent(tile, &enter); break;
                    }
                }
                if (parser.isSet("mosaic-end")) { auto *explore = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype")); explore->ensureWidgetVisible(explore->sectionTiles().last()); }
                if (parser.isSet("explore-collection")) static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"))->showCollection();
                if (parser.isSet("explore-bottom")) static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"))->showDirectory();
                if (parser.isSet("explore-end")) {
                    auto *page = static_cast<ExplorePage *>(store.findChild<QWidget *>("explorePrototype"));
                    page->verticalScrollBar()->setValue(page->verticalScrollBar()->maximum());
                }
                if (parser.isSet("list-selected")) store.previewListSelection();
            });
            QTimer::singleShot(parser.isSet("mosaic-hover") ? 1150 : 800, &store, capture);
        }
        if (!parser.isSet("live-catalog")) QObject::connect(&store.backend, &Backend::catalogueChanged, &store, [&store, capture] {
            if (!store.backend.catalogue.isEmpty() && !store.backend.installed.isEmpty() && !store.backend.catalogue.first().remote.isEmpty()) QTimer::singleShot(700, &store, capture);
        });
        if (!parser.isSet("live-catalog")) QObject::connect(&store.backend, &Backend::installedChanged, &store, [&store, capture] {
            if (!store.backend.catalogue.isEmpty() && !store.backend.installed.isEmpty() && !store.backend.catalogue.first().remote.isEmpty()) QTimer::singleShot(700, &store, capture);
        });
    }
    return application.exec();
}

#include "main.moc"
