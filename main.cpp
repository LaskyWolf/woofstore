#include "backend.h"
#include "explore.h"
#include "appdetails.h"
#include <QtWidgets>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KParts/ReadOnlyPart>
#include <kde_terminal_interface.h>
#include <unistd.h>

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
        compactSidebar = new QAction(QIcon::fromTheme("sidebar-collapse-left"), "Compact sidebar", this);
        compactSidebar->setObjectName("compactSidebar");
        compactSidebar->setCheckable(true);
        auto *sidebarToggle = new QToolButton;
        sidebarToggle->setObjectName("sidebarToggle");
        sidebarToggle->setAutoRaise(true);
        sidebarToggle->setDefaultAction(compactSidebar);
        sidebarToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
        sidebarToggle->setFixedSize(40, 40);
        side->addWidget(sidebarToggle, 0, Qt::AlignLeft);
        const QString navigationStyle = "QToolButton {border: none; border-radius: 8px; padding: 10px; background: transparent;} QToolButton:hover {background: palette(button);} QToolButton:checked {background: palette(midlight);} QToolButton:focus {border: 1px solid palette(highlight);}";
        sidebarToggle->setStyleSheet(navigationStyle);
        navigation = new QButtonGroup(this); navigation->setObjectName("pageNavigation"); navigation->setExclusive(true);
        auto *navHost = new QWidget; navHost->setObjectName("navigation");
        auto *navLayout = new QVBoxLayout(navHost); navLayout->setContentsMargins(0, 0, 0, 0); navLayout->setSpacing(8);
        int id = 0;
        for (const auto &entry : QList<QPair<QString, QString>>{{"Explore", "view-grid"}, {"Installed", "view-list-details"}, {"Updates", "view-refresh"}}) {
            auto *item = new QToolButton;
            item->setObjectName("nav" + entry.first); item->setText(entry.first); item->setIcon(QIcon::fromTheme(entry.second));
            item->setIconSize(QSize(20, 20)); item->setFixedHeight(40); item->setCheckable(true);
            item->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); item->setStyleSheet(navigationStyle);
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
            activityButton->setFixedHeight(compact ? sidebarToggle->height() : 48);
            activityButton->update();
            for (auto *button : navigation->buttons()) {
                auto *item = static_cast<QToolButton *>(button);
                item->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            }
            compactSidebar->setIcon(QIcon::fromTheme(compact ? "sidebar-expand-left" : "sidebar-collapse-left"));
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
            if (part && !packageActivityHistory.isEmpty()) packageActivityHistory = {packageActivityHistory.last()};
            else packageActivityHistory.clear();
            refreshActivity();
        });
        connect(back, &QPushButton::clicked, activityDialog, &QDialog::hide);
        auto *popupLayout = new QVBoxLayout(activityDialog); popupLayout->setContentsMargins(0, 0, 0, 0); popupLayout->addWidget(activity);
        pages->addWidget(new QWidget); // Keep the existing app-details stack index.
        previewOperations.onChanged = [this] { refreshActivity(); };
        previewOperations.onOpen = [this](const QString &name) { statusBar()->showMessage(name + " · no application launched", 4000); };
        refreshActivity();
        appDetails = new AppDetailsPage;
        appDetails->operations = &previewOperations;
        appDetails->setObjectName("appDetailsPage");
        pages->addWidget(appDetails);
        history.append({pages->currentWidget()}); historyIndex = 0;
        appDetails->backend = &backend;
        explore->onOpen = [this](const CatalogApp &app) { openDetails(app); };
        appDetails->onBack = [this] { moveHistory(-1); };
        appDetails->onOpenAddon = [this](const CatalogApp &app) { openDetails(app); };
        qApp->installEventFilter(this);
        connect(navigation, &QButtonGroup::idClicked, this, &Store::selectPage);
        connect(&backend, &Backend::installedChanged, this, [this] { renderInstalled(); });
        connect(&backend, &Backend::issue, this, [this](const QString &message) {
            statusBar()->showMessage(message);
        });
        connect(&backend, &Backend::catalogReady, this, [this] {
            explore->notice->setText("Repository apps have no icons or descriptions yet. Install them with: sudo pacman -S --needed archlinux-appstream-data");
            explore->notice->setVisible(backend.osMetadataMissing);
            explore->setCatalog();
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
        updateAll->setEnabled(state != "checking"); checkUpdates->setEnabled(state != "checking"); findChild<QPushButton *>("runUpdates")->setEnabled(state != "checking"); updateStatus->setText("Not checked"); updateList->clear();
        if (state == "checking") { checkUpdates->setEnabled(false); updateStatus->setText("Checking…"); updateList->setPlainText("Checking system, AUR, and Flatpak updates…"); }
        if (state == "failed") { updateStatus->setText("Check failed"); updateList->setPlainText("No installation started. Try checking again.\n\nSample: package service unavailable."); }
    }
    void previewListSelection() {
        auto *list = pages->currentWidget() == explore ? explore->results : installedList;
        if (list->count()) { list->setCurrentRow(0); list->setFocus(); }
    }

    void showActivity() { activityDialog->show(); activityDialog->raise(); activityDialog->activateWindow(); }
    void previewActivity() { previewOperations.start("sample", "Krita", "Install"); showActivity(); }
    void refreshActivity() {
        if (!activityHistory) return;
        struct Record { QString name, state; int progress; bool unknown; };
        QList<Record> records;
        for (const auto &entry : packageActivityHistory) records.append({entry, "Package operation", 0, bool(part) && entry == packageActivityHistory.last()});
        for (const auto &activity : previewOperations.activities) {
            if (activity.cleared) continue;
            const QString state = activity.progress == 100 ? "Complete" : QString::number(activity.progress) + "%";
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
            }
        }
        for (int i = 0; i < records.size(); ++i) {
            const auto &record = records[i]; auto *row = activityHistory->itemWidget(activityHistory->item(i));
            row->findChild<QLabel *>("activityName")->setText(record.name);
            row->findChild<QLabel *>("activityState")->setText(record.state);
            auto *progress = row->findChild<QProgressBar *>("activityProgress");
            progress->setRange(0, record.unknown ? 0 : 100); progress->setValue(record.progress);
            progress->setVisible(record.unknown || record.progress > 0);
        }
        terminalHost->setVisible(bool(part));
        const bool completed = std::any_of(previewOperations.activities.cbegin(), previewOperations.activities.cend(), [](const auto &entry) { return !entry.cleared && entry.progress == 100; });
        auto *clear = activityDialog->findChild<QPushButton *>("clearActivity");
        const bool canClear = completed || packageActivityHistory.size() > (part ? 1 : 0);
        clear->setEnabled(canClear); clear->setCursor(canClear ? Qt::PointingHandCursor : Qt::ArrowCursor);
        static_cast<QGraphicsOpacityEffect *>(clear->graphicsEffect())->setOpacity(canClear ? 1. : .4);
        if (!part) activityStatus->setText(previewOperations.running() ? "No package changes" : "Activity history");
        activityButton->busy = bool(part) || previewOperations.running(); activityButton->indeterminate = bool(part);
        activityButton->progress = previewOperations.activities.isEmpty() ? 0 : previewOperations.progress();
        activityButton->setToolTip(part ? "Package activity running · progress unavailable" : previewOperations.running() ? QString("Activity · %1%").arg(activityButton->progress) : "Show activity");
        const auto *updates = previewOperations.latest("preview-system-updates");
        const bool updating = updates && updates->progress < 100;
        if (updating || previewUpdateRunning) {
            updateAll->setText(updating ? "Updating…" : "Update all"); updateAll->setEnabled(!updating);
            checkUpdates->setEnabled(!updating); findChild<QPushButton *>("runUpdates")->setEnabled(!updating);
            updateStatus->setText(updating ? "Updating…" : "Complete");
            if (!updating) updateList->appendPlainText("Complete · no system changes.");
            previewUpdateRunning = updating;
        }
        activityButton->update();
    }

    void previewFiles(const QList<QUrl> &files) {
        int added = 0, skipped = 0;
        for (const auto &url : files) {
            if (!FileDropPage::accepts(url)) { statusBar()->showMessage("Choose a local AppImage, .flatpak, or .flatpakref file."); continue; }
            const QFileInfo file(url.toLocalFile()); const QString path = file.canonicalFilePath();
            bool duplicate = false;
            for (const auto &existing : filePreviews) if (existing.path == path) duplicate = true;
            if (duplicate) { ++skipped; continue; }
            App app; app.path = path; app.id = "preview-file-" + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
            app.name = file.completeBaseName(); app.source = file.suffix().compare("appimage", Qt::CaseInsensitive) == 0 ? "AppImage" : "Flatpak";
            app.remote = "local file"; app.scope = "local"; app.summary = "File installation · no package changes";
            app.icon = QIcon::fromTheme("application-x-executable"); filePreviews.append(app);
            previewOperations.start(app.id, app.name, "Install"); ++added;
        }
        if (!added && skipped) statusBar()->showMessage("These files are already in the list.");
        if (added) {
            installedSearch->clear(); installedFilter->setCurrentIndex(0); renderInstalled();
            statusBar()->showMessage(QString("%1 file(s) added · no package changes").arg(added));
        }
    }
    void showUpdateAuthorization() {
        if (const auto *last = previewOperations.latest("preview-system-updates"); last && last->progress < 100) { showActivity(); return; }
        if (updateAuthorization) { updateAuthorization->show(); updateAuthorization->raise(); return; }
        auto *dialog = new QDialog(this); updateAuthorization = dialog; dialog->setObjectName("updateAuthorization");
        dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setModal(true); dialog->setWindowTitle("Confirm updates"); dialog->resize(420, 220);
        auto *layout = new QVBoxLayout(dialog); layout->setContentsMargins(20, 16, 20, 20); layout->setSpacing(16);
        layout->addWidget(label("Confirm updates", 18, true));
        layout->addWidget(label("Enter sample text. No system authorization occurs."));
        auto *password = new QLineEdit; password->setObjectName("previewPassword"); password->setEchoMode(QLineEdit::Password);
        password->setPlaceholderText("Sample password"); password->setAccessibleName("Sample password"); password->setMinimumHeight(36); layout->addWidget(password);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
        auto *accept = buttons->button(QDialogButtonBox::Ok); accept->setText("Accept updates"); accept->setObjectName("acceptPreviewUpdates"); accept->setEnabled(false); layout->addWidget(buttons);
        connect(password, &QLineEdit::textChanged, accept, [accept](const QString &value) { accept->setEnabled(!value.isEmpty()); });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        connect(dialog, &QDialog::accepted, this, [this, password] {
            password->clear(); previewOperations.start("preview-system-updates", "System updates", "Update");
        });
        connect(dialog, &QDialog::finished, password, [password] { password->clear(); });
        dialog->show(); password->setFocus();
    }

    bool startActivity(const QString &title, const QString &program, const QStringList &arguments = {}, const QString &cwd = {}) {
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
        packageActivityHistory.append(title + " · Running · progress unavailable");
        activityStatus->setText("Answer questions directly below. Passwords are handled by the package manager. Keep this window open until the process finishes.");
        showActivity();
        statusBar()->showMessage(title + " — running");
        refreshActivity();
        connect(part, &QObject::destroyed, this, [this] {
            part = nullptr;
            if (!packageActivityHistory.isEmpty()) packageActivityHistory.last() = activityTitle->text() + " · Ended · check command exit code";
            activityStatus->setText("Activity ended. Check the exit code in its output before retrying.");
            refreshActivity();
            statusBar()->showMessage("Activity ended");
            backend.loadInstalled();
        });
        // Fixed wrapper; package names, paths and arguments are positional data, never shell code.
        const QString wrapper = "cd -- \"$1\" || exit 1; shift; \"$@\"; code=$?; printf '\\nCommand exit code: %s\\nPress Enter to close this activity.' \"$code\"; IFS= read -r answer; exit \"$code\"";
        // KonsolePart expects argv[0] in its argument list (unlike QProcess).
        QStringList args = {"/usr/bin/bash", "-c", wrapper, "woof-store-activity", cwd.isEmpty() ? QDir::homePath() : cwd, executable};
        args.append(arguments);
        terminal->startProgram("/usr/bin/bash", args);
        part->widget()->setFocus();
        return true;
    }

protected:
    // The compositor owns the window edge; omit the themed content-top frame.
    void paintEvent(QPaintEvent *) override {}
    void closeEvent(QCloseEvent *event) override {
        if (part) {
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
    QPushButton *updateAll = nullptr;
    QSettings settings;
    QWidget *sidebar = nullptr;
    QFrame *sidebarDivider = nullptr;
    QPointer<QDialog> updateAuthorization;
    bool previewUpdateRunning = false;
    Apps filePreviews, displayedInstalled;
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
        if (widget && widget->window() == this && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseButtonRelease)) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::BackButton || mouse->button() == Qt::ForwardButton) {
                if (event->type() != QEvent::MouseButtonRelease) moveHistory(mouse->button() == Qt::BackButton ? -1 : 1);
                event->accept(); return true;
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
    QPlainTextEdit *updateList = nullptr;
    QLabel *updateStatus = nullptr;
    QPushButton *checkUpdates = nullptr;
    QPointer<KParts::ReadOnlyPart> part;

    QWidget *scrollPage(QVBoxLayout **content, QScrollArea *supplied = nullptr) {
        auto *scroll = supplied ? supplied : new QScrollArea;
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
        auto *dropPage = new FileDropPage; dropPage->setObjectName("installedDropPage");
        dropPage->onFiles = [this](const QList<QUrl> &files) { previewFiles(files); };
        installedBody = scrollPage(&content, dropPage);
        installedBody->setObjectName("installedPageBody");
        content->setContentsMargins(20, 16, 20, 24); content->setSpacing(16);
        auto *row = new QHBoxLayout;
        row->addWidget(label("Installed", 23, true), 1);
        auto *import = button("Add AppImage", "list-add");
        import->setObjectName("addAppImage");
        row->addWidget(import);
        auto *addFlatpak = button("Add Flatpak", "list-add"); addFlatpak->setObjectName("addFlatpak"); row->addWidget(addFlatpak);
        auto *refresh = button("Refresh", "view-refresh");
        row->addWidget(refresh);
        content->addLayout(row);
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
        installedFilter->addItems({"All sources", "Flatpak", "AUR / local", "AppImage"});
        installedFilter->setMinimumHeight(32);
        installedFilter->setAccessibleName("Installed software source");
        searchRow->addWidget(installedSearch, 1);
        searchRow->addWidget(installedFilter);
        content->addWidget(searchTray);
        installedLabel = label("Loading installed software…", 0, true);
        content->addWidget(installedLabel);
        installedList = new AppList; installedList->setObjectName("installedApps"); installedList->setAccessibleName("Installed apps, alphabetically sorted"); content->addWidget(installedList);
        installedEmpty = label("Drop an AppImage or Flatpak file here, or use Add AppImage / Add Flatpak."); content->addWidget(installedEmpty); installedEmpty->hide();
        connect(installedList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { openInstalledItem(item); });
        connect(installedList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { openInstalledItem(item); });
        content->addStretch();
        connect(installedSearch, &QLineEdit::textChanged, this, &Store::renderInstalled);
        connect(installedFilter, &QComboBox::currentIndexChanged, this, &Store::renderInstalled);
        connect(refresh, &QPushButton::clicked, &backend, &Backend::loadInstalled);
        auto chooseFiles = [this](const QString &title, const QString &filter) {
            const auto files = QFileDialog::getOpenFileNames(this, title, QDir::homePath(), filter);
            QList<QUrl> urls; for (const auto &file : files) urls.append(QUrl::fromLocalFile(file));
            previewFiles(urls);
        };
        import->setToolTip("Add an AppImage · no file changes");
        addFlatpak->setToolTip("Add a Flatpak · no package changes");
        connect(import, &QPushButton::clicked, this, [chooseFiles] { chooseFiles("Add AppImage", "AppImages (*.AppImage *.appimage)"); });
        connect(addFlatpak, &QPushButton::clicked, this, [chooseFiles] { chooseFiles("Add Flatpak", "Flatpak files (*.flatpak *.flatpakref)"); });
    }

    void buildUpdates() {
        QVBoxLayout *content;
        auto *body = scrollPage(&content); body->setObjectName("updatesPageBody");
        content->setContentsMargins(20, 16, 20, 24); content->setSpacing(16);
        auto *heading = new QHBoxLayout;
        heading->addWidget(label("Updates", 23, true), 1);
        checkUpdates = button("Check for updates", "view-refresh"); checkUpdates->setObjectName("checkUpdates");
        auto *update = button("Review & update", "system-software-update"); update->setObjectName("runUpdates");
        content->addLayout(heading);
        auto *results = new DetailPanel; results->setObjectName("updateResults");
        auto *resultsLayout = new QVBoxLayout(results); resultsLayout->setContentsMargins(14, 14, 14, 14); resultsLayout->setSpacing(12);
        auto *resultsHeading = new QHBoxLayout; resultsHeading->addWidget(label("Available updates", 12, true), 1);
        updateStatus = label("Not checked"); updateStatus->setObjectName("updateStatus"); resultsHeading->addWidget(updateStatus); resultsLayout->addLayout(resultsHeading);
        updateList = new QPlainTextEdit; updateList->setObjectName("updateOutput"); updateList->setReadOnly(true); updateList->setFrameShape(QFrame::NoFrame);
        updateList->setStyleSheet("QPlainTextEdit { background: transparent; border: none; }");
        updateList->setAccessibleName("Available updates"); updateList->setPlaceholderText("Check for updates to see available packages."); updateList->setMinimumHeight(220);
        resultsLayout->addWidget(updateList, 1); content->addWidget(results, 1);
        auto *actions = new DetailPanel; actions->setObjectName("updateActions");
        auto *actionsLayout = new QVBoxLayout(actions); actionsLayout->setContentsMargins(14, 14, 14, 14); actionsLayout->setSpacing(10);
        updateAll = button("Update all", "system-software-update"); updateAll->setObjectName("updateAll"); updateAll->setMinimumHeight(44); updateAll->setDefault(true);
        update->setText("Review updates");
        auto *actionsRow = new QHBoxLayout; actionsRow->addWidget(checkUpdates); actionsRow->addWidget(update);
        actionsLayout->addWidget(updateAll); actionsLayout->addLayout(actionsRow); content->addWidget(actions);
        connect(checkUpdates, &QPushButton::clicked, this, [this, update] {
            if (part) { showActivity(); return; }
            checkUpdates->setEnabled(false); update->setEnabled(false); updateAll->setEnabled(false);
            updateStatus->setText("Checking…");
            updateList->setPlainText("Checking system, AUR, and Flatpak updates…");
            backend.readCommand("arch-update", {"--list"}, [this, update](int code, QString output, QString error) {
                checkUpdates->setEnabled(true); update->setEnabled(true); updateAll->setEnabled(true);
                static const QRegularExpression ansi("\\x1b\\[[0-?]*[ -/]*[@-~]");
                output.remove(ansi);
                if (code == 0 || code == 7) {
                    updateStatus->setText("Check complete"); updateList->setPlainText(output.trimmed());
                    updateList->setPlaceholderText("No updates reported.");
                } else {
                    updateStatus->setText("Check failed");
                    updateList->setPlainText("No installation started. Try checking again.\n\n" + error + '\n' + output);
                }
            }, 120000);
        });
        connect(update, &QPushButton::clicked, this, [this] {
            if (!checkUpdates->isEnabled()) { QMessageBox::information(this, "Update check running", "Wait for the current update check to finish."); return; }
            showUpdateAuthorization();
        });
        connect(updateAll, &QPushButton::clicked, update, &QPushButton::click);
    }

    void openInstalledItem(QListWidgetItem *item) {
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= displayedInstalled.size()) return;
        const auto app = displayedInstalled[index];
        for (const auto &entry : backend.apps)
            for (const auto &package : entry.packages)
                if (package.name == app.id && (package.source == Source::Flatpak) == (app.source == "Flatpak")) { openDetails(entry, app.source == "Flatpak" ? "Flatpak · " + package.repo : QString()); return; }
        // Installed outside the catalogue: AppImages, local files, removed remotes.
        CatalogApp local; local.key = "installed:" + app.id; local.name = app.name;
        local.summary = app.summary.isEmpty() ? "Description unavailable for this app." : app.summary; local.homepage = app.homepage;
        openDetails(local, app.source);
    }

    void renderInstalled() {
        if (!installedList || !installedSearch || !installedFilter) return;
        displayedInstalled = backend.installed; displayedInstalled.append(filePreviews);
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
            installedList->addApp(app.name, metadata.join(" · "), icon, index, {}, &previewOperations, app.id, true);
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
    parser.addOption({"authorization-preview", "Preview the sample-password update dialog."});
    parser.addOption({"file-preview", "Preview file installation without package changes.", "path"});
    parser.addOption({"drop-overlay", "Show the Installed drag overlay for a local file.", "path"});
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
        QTemporaryDir droppedFiles;
        QStringList paths;
        for (const QString &name : {QString("Zzz-preview.AppImage"), QString("Zzz-flatpak.flatpakref")}) {
            const QString path = droppedFiles.path() + '/' + name;
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) return 1;
            if (file.write("Preview fixture only") < 0) return 1;
            file.close(); paths.append(path);
        }
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(paths[0]), QUrl::fromLocalFile(paths[1])});
        auto *dropPage = store.findChild<QScrollArea *>("installedDropPage");
        // Retain the Qt 6.6 event constructor in this compatibility check.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dropPage->viewport(), &enter); passed &= enter.isAccepted();
        auto *overlay = static_cast<FileDropOverlay *>(store.findChild<QWidget *>("fileDropOverlay"));
        passed &= overlay && overlay->isVisible() && overlay->title == "Drop to install AppImage / Flatpak"
            && overlay->geometry() == store.centralWidget()->rect();
        QDragLeaveEvent leave; QApplication::sendEvent(dropPage->viewport(), &leave);
        passed &= !overlay->isVisible();
        QApplication::sendEvent(dropPage->viewport(), &enter);
        QDropEvent drop(QPointF(40, 40), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dropPage->viewport(), &drop);
        passed &= drop.isAccepted() && installed->count() == 5 && !overlay->isVisible();
        store.previewFiles(mime.urls()); passed &= installed->count() == 5;
        QMimeData remote; remote.setUrls({QUrl("https://example.invalid/app.flatpak")});
        QDragEnterEvent rejected(QPoint(40, 40), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dropPage->viewport(), &rejected); passed &= !rejected.isAccepted() && !overlay->isVisible();
        QT_WARNING_POP
        installed->itemClicked(installed->item(installed->count() - 1));
        passed &= details->isVisible() && details->sourceButton->text() == "AppImage";
        details->back->click();
        store.backend.installedChanged(); passed &= installed->count() == 5;
        passed &= page->loadMore->isHidden();  // The fixture fits in one page
        store.findChild<QPushButton *>("updateAll")->click();
        auto *auth = store.findChild<QDialog *>("updateAuthorization");
        auto *password = auth->findChild<QLineEdit *>("previewPassword");
        auto *accept = auth->findChild<QPushButton *>("acceptPreviewUpdates");
        passed &= auth->isVisible() && password->echoMode() == QLineEdit::Password && !accept->isEnabled();
        password->setText("sample-cancel"); auth->reject();
        passed &= password->text().isEmpty() && store.findChild<QPushButton *>("updateAll")->isEnabled();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        store.findChild<QPushButton *>("updateAll")->click();
        auth = store.findChild<QDialog *>("updateAuthorization"); password = auth->findChild<QLineEdit *>("previewPassword");
        accept = auth->findChild<QPushButton *>("acceptPreviewUpdates");
        password->setText("sample"); passed &= accept->isEnabled(); accept->click();
        passed &= password->text().isEmpty() && !store.findChild<QPushButton *>("updateAll")->isEnabled();
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
            auto *list = static_cast<AppList *>(store.findChild<QListWidget *>("installedApps"));
            auto *remove = list->findChild<QPushButton *>("quickRemove");
            remove->click(); result &= !remove->isEnabled() && remove->text() == "Removing…";
            QTimer::singleShot(2300, &store, [&application, &store, result, remove, list] {
                bool removed = result && remove->text() == "Removed" && !remove->isEnabled();
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
        page->showDirectory(); QApplication::processEvents(); passed &= !autoplay->isActive();
        page->verticalScrollBar()->setValue(0); QApplication::processEvents(); passed &= autoplay->isActive();
        auto *tile = page->sections.first().masonry->tiles[0];  // GIMP: the featured install above already finished Krita page->ensureWidgetVisible(tile); QApplication::processEvents();
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
        auto *installedPage = store.findChild<QScrollArea *>("installedDropPage");
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
        passed &= !store.findChild<QPushButton *>("checkUpdates")->isEnabled() && store.findChild<QLabel *>("updateStatus")->text() == "Checking…";
        store.previewUpdates("failed");
        passed &= store.findChild<QPushButton *>("checkUpdates")->isEnabled() && store.findChild<QLabel *>("updateStatus")->text() == "Check failed";
        store.previewUpdates("idle"); passed &= store.findChild<QPlainTextEdit *>("updateOutput")->isReadOnly();
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
        bool passed = store.findChild<QAbstractButton *>("activityButton")->height() == toggle->height() && side->width() == 56 && store.findChild<QFrame *>("sidebarDivider")->isHidden() && nav->checkedId() == 2 && !store.findChild<QToolButton *>("settingsButton");
        for (auto *button : nav->buttons()) {
            auto *item = static_cast<QToolButton *>(button);
            passed &= item->toolButtonStyle() == Qt::ToolButtonIconOnly && item->height() == 40 && !item->accessibleName().isEmpty();
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
    if (parser.isSet("file-preview")) { store.selectPage(1); store.previewFiles({QUrl::fromLocalFile(parser.value("file-preview"))}); }
    if (parser.isSet("drop-overlay")) {
        store.previewInstalled(); QApplication::processEvents();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(parser.value("drop-overlay"))});
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QT_WARNING_POP
        QApplication::sendEvent(store.findChild<QScrollArea *>("installedDropPage")->viewport(), &enter);
    }
    if (parser.isSet("installed-open")) {
        auto *list = store.findChild<QListWidget *>("installedApps");
        if (list->count()) {
            auto *item = list->item(0);
            if (parser.isSet("file-preview")) for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text().section('\n', 0, 0) == QFileInfo(parser.value("file-preview")).completeBaseName()) item = list->item(i);
            list->itemClicked(item);
        }
    }
    if (parser.isSet("authorization-preview")) { store.selectPage(2); store.showUpdateAuthorization(); }
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
            if (parser.isSet("authorization-preview")) return store.findChild<QDialog *>("updateAuthorization")->grab().save(parser.value("screenshot"));
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
