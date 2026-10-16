#pragma once
#include "explore.h"
#include <functional>

class AppScreenshot : public QWidget {
public:
    QList<QPixmap> images;
    int current = 0;
    std::function<void(int)> onChanged;
    explicit AppScreenshot(QWidget *parent = nullptr) : QWidget(parent) {
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred); policy.setHeightForWidth(true); setSizePolicy(policy);
        setAccessibleName("App screenshots; use Left and Right to browse"); setFocusPolicy(Qt::StrongFocus);
        animation.setDuration(300); animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { position = value.toReal(); update(); });
        connect(&animation, &QVariantAnimation::finished, this, [this] { position = current; update(); });
    }
    void setImages(const QList<QPixmap> &value) { animation.stop(); images = value; current = 0; position = 0; updateGeometry(); update(); }
    void select(int index, bool animate = true) {
        if (images.isEmpty()) return;
        const int count = images.size(); index = (index % count + count) % count;
        animation.stop(); current = index;
        qreal target = index;
        while (target - position > count / 2.) target -= count;
        while (position - target > count / 2.) target += count;
        if (animate) { animation.setStartValue(position); animation.setEndValue(target); animation.start(); }
        else { position = index; update(); }
        if (onChanged) onChanged(index);
    }
    int heightForWidth(int width) const override { return qBound(220, int(width * .5), 420); }
    QSize sizeHint() const override { return QSize(920, 420); }
    QSize minimumSizeHint() const override { return QSize(0, 220); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.setRenderHint(QPainter::Antialiasing);
        if (images.isEmpty()) return;
        QList<int> order;
        if (images.size() == 1) order.append(0);
        else for (int i = int(std::floor(position)) - 2; i <= int(std::floor(position)) + 2; ++i) order.append(i);
        std::sort(order.begin(), order.end(), [this](int a, int b) { return qAbs(a - position) > qAbs(b - position); });
        for (int i : order) {
            const qreal delta = i - position;
            if (qAbs(delta) > 1.8) continue;
            const QSizeF bounds(width() * .76, height() * .96);
            const QPixmap &image = images[(i % images.size() + images.size()) % images.size()];
            const QSizeF size = QSizeF(image.size()).scaled(bounds, Qt::KeepAspectRatio);
            const qreal x = width() / 2. + delta * (width() * .76 + 12), y = height() / 2.;
            const QRectF target(x - size.width() / 2, y - size.height() / 2, size.width(), size.height());
            painter.drawPixmap(target.toRect(), image);
        }
        if (hasFocus() && keyboardFocus) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.drawLine(4, height() - 2, width() - 4, height() - 2);
        }
    }
    void focusInEvent(QFocusEvent *event) override { keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason; QWidget::focusInEvent(event); update(); }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) select(current + (event->key() == Qt::Key_Right ? 1 : -1));
        else QWidget::keyPressEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override {
        for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *page = qobject_cast<QAbstractScrollArea *>(parent)) {
                QApplication::sendEvent(page->verticalScrollBar(), event); return;
            }
        }
        event->ignore();
    }
    void mousePressEvent(QMouseEvent *event) override { start = event->position(); setFocus(); }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const qreal drag = event->position().x() - start.x();
        if (qAbs(drag) > 30) select(current + (drag < 0 ? 1 : -1));
        else if (event->position().x() < width() * .2) select(current - 1);
        else if (event->position().x() > width() * .8) select(current + 1);
    }
private:
    QVariantAnimation animation;
    qreal position = 0;
    QPointF start;
    bool keyboardFocus = false;
};

class DetailPanel : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = palette().color(QPalette::WindowText); fill.setAlpha(14);
        painter.setPen(Qt::NoPen); painter.setBrush(fill); painter.drawRoundedRect(rect(), 12, 12);
    }
};

class AppDetailsPage : public QScrollArea {
public:
    std::function<void()> onBack;
    std::function<void(const DemoApp &)> onOpenAddon;
    QToolButton *back = nullptr;
    QLabel *title = nullptr;
    AppScreenshot *screenshot = nullptr;
    QString currentId;
    PreviewOperations *operations = nullptr;
    QPushButton *install = nullptr;
    QToolButton *sourceButton = nullptr;
    explicit AppDetailsPage(QWidget *parent = nullptr) : QScrollArea(parent) {
        setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget; setWidget(body);
        auto *outer = new QVBoxLayout(body); outer->setContentsMargins(28, 16, 28, 32); outer->setSpacing(24);
        back = new QToolButton; back->setObjectName("detailsBack"); back->setAutoRaise(false); back->setMinimumHeight(36);
        back->setIcon(QIcon::fromTheme("go-previous")); back->setText("Explore"); back->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        back->setAccessibleName("Back to Explore"); back->setToolTip("Back to Explore (Alt+Left)");
        auto *toolbar = new QHBoxLayout; toolbar->addWidget(back); toolbar->addStretch();
        install = new QPushButton("Install"); install->setObjectName("previewInstall"); install->setMinimumHeight(36); toolbar->addWidget(install);
        sourceButton = new QToolButton; sourceButton->setObjectName("previewSource"); sourceButton->setText("Flatpak"); sourceButton->setMinimumHeight(36);
        sourceButton->setPopupMode(QToolButton::MenuButtonPopup); sourceButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(sourceButton, &QToolButton::clicked, sourceButton, &QToolButton::showMenu);
        sourceButton->setIcon(QIcon::fromTheme("package-x-generic")); sourceButton->setAccessibleName("Choose source");
        auto *sources = new QMenu(sourceButton); auto *sourceGroup = new QActionGroup(sources); sourceGroup->setExclusive(true);
        for (const QString &source : {QString("Flatpak"), QString("AUR")}) {
            auto *action = sources->addAction(source); action->setCheckable(true); action->setChecked(source == "Flatpak"); sourceGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, source] { sourceButton->setText(source); });
        }
        sourceButton->setMenu(sources); sourceButton->setToolTip("Choose software source"); toolbar->addWidget(sourceButton); outer->addLayout(toolbar);
        connect(back, &QToolButton::clicked, this, [this] { if (onBack) onBack(); });
        auto *shortcut = new QShortcut(QKeySequence("Alt+Left"), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut); connect(shortcut, &QShortcut::activated, back, &QToolButton::click);
        content = new QWidget; content->setFixedWidth(920);
        auto *column = new QVBoxLayout(content); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(24);
        auto *header = new QHBoxLayout; header->setSpacing(22);
        icon = new QLabel; icon->setFixedSize(96, 96); header->addWidget(icon, 0, Qt::AlignTop);
        auto *copy = new QVBoxLayout; copy->setSpacing(8);
        title = text("", 28, true); title->setObjectName("detailsTitle"); summary = text("", 12);
        copy->addWidget(title); copy->addWidget(summary); copy->addStretch(); header->addLayout(copy, 1); column->addLayout(header);
        description = text(""); description->setObjectName("detailsDescription"); description->setTextInteractionFlags(Qt::TextSelectableByMouse);
        description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        descriptionMotion.setParent(this); descriptionMotion.setObjectName("descriptionMotion"); descriptionMotion.setEasingCurve(QEasingCurve::OutCubic);
        connect(&descriptionMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { description->setFixedHeight(value.toInt()); });
        auto *descriptionLayout = new QVBoxLayout; descriptionLayout->setSpacing(8);
        descriptionLayout->addWidget(description);
        descriptionToggle = new QToolButton; descriptionToggle->setObjectName("descriptionToggle");
        descriptionToggle->setAutoRaise(false); descriptionToggle->setMinimumHeight(36); descriptionToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        descriptionLayout->addWidget(descriptionToggle, 0, Qt::AlignLeft); column->addLayout(descriptionLayout);
        connect(descriptionToggle, &QToolButton::clicked, this, [this] { descriptionExpanded = !descriptionExpanded; updateDescription(true); });
        screenshot = new AppScreenshot; column->addWidget(screenshot);
        galleryControls = new QWidget; galleryLayout = new QHBoxLayout(galleryControls); galleryLayout->setContentsMargins(0, 0, 0, 0); galleryLayout->setSpacing(4); column->addWidget(galleryControls);
        screenshot->onChanged = [this](int index) { for (int i = 0; i < dots.size(); ++i) dots[i]->setChecked(i == index); };
        noScreenshot = text("No publisher screenshot available."); column->addWidget(noScreenshot);
        auto *changelogPanel = new DetailPanel; changelogPanel->setObjectName("changelogPanel");
        auto *changelogLayout = new QVBoxLayout(changelogPanel); changelogLayout->setContentsMargins(24, 24, 24, 24); changelogLayout->setSpacing(12);
        changelogLayout->addWidget(text("Latest changelog", 16, true));
        releaseVersion = text("", 0, true); releaseVersion->setObjectName("releaseVersion"); changelogLayout->addWidget(releaseVersion);
        changelog = text(""); changelog->setObjectName("releaseNotes"); changelog->setTextInteractionFlags(Qt::TextSelectableByMouse); changelogLayout->addWidget(changelog);
        column->addWidget(changelogPanel);
        auto *addonPanel = new DetailPanel; addonPanel->setObjectName("addonPanel");
        addonLayout = new QVBoxLayout(addonPanel); addonLayout->setContentsMargins(24, 24, 24, 24); addonLayout->setSpacing(12);
        column->addWidget(addonPanel);
        auto *permissionPanel = new DetailPanel; permissionPanel->setObjectName("permissionPanel");
        auto *panelLayout = new QVBoxLayout(permissionPanel); panelLayout->setContentsMargins(24, 24, 24, 24); panelLayout->setSpacing(18);
        permissionsHeading = text("Permissions", 16, true); panelLayout->addWidget(permissionsHeading);
        permissionNote = text(""); panelLayout->addWidget(permissionNote);
        permissionBody = new QWidget; permissionLayout = new QVBoxLayout(permissionBody); permissionLayout->setContentsMargins(0, 0, 0, 0); permissionLayout->setSpacing(18);
        panelLayout->addWidget(permissionBody); column->addWidget(permissionPanel);
        auto *linksPanel = new DetailPanel; linksPanel->setObjectName("linksPanel");
        linksLayout = new QVBoxLayout(linksPanel); linksLayout->setContentsMargins(24, 24, 24, 24); linksLayout->setSpacing(12);
        column->addWidget(linksPanel);
        outer->addWidget(content, 0, Qt::AlignHCenter); outer->addStretch();
        QFile metadata(":/explore/details.json"); if (metadata.open(QIODevice::ReadOnly)) data = QJsonDocument::fromJson(metadata.readAll()).object();
    }
    void showApp(const DemoApp &app, const QIcon &fallbackIcon = {}, const QString &platform = "Flatpak") {
        // Rebind per app; keep progress in the shared preview controller.
        if (installBinding) disconnect(installBinding);
        if (operations) installBinding = operations->bind(install, app.id, app.name);
        auto *menu = sourceButton->menu(); QAction *choice = nullptr;
        for (auto *action : menu->actions()) if (action->text() == platform) choice = action;
        if (!choice) {
            choice = menu->addAction(platform); choice->setCheckable(true);
            menu->actions().first()->actionGroup()->addAction(choice);
            connect(choice, &QAction::triggered, this, [this, platform] { sourceButton->setText(platform); });
        }
        choice->setChecked(true); sourceButton->setText(platform);
        currentId = app.id; title->setText(app.name); summary->setText(app.summary);
        QIcon symbol(":/explore/" + app.id + ".png");
        if (symbol.isNull()) symbol = fallbackIcon.isNull() ? QIcon::fromTheme("application-x-executable") : fallbackIcon;
        icon->setPixmap(symbol.pixmap(96, 96));
        const auto info = data.value(app.id).toObject();
        description->setText(info.value("description").toString(app.summary));
        if (description->text().isEmpty()) description->setText(app.summary);
        descriptionExpanded = false; updateDescription();
        QList<QPixmap> images;
        for (const auto &file : info.value("screenshots").toArray()) {
            QPixmap image(":/explore/" + file.toString()); if (!image.isNull()) images.append(image);
        }
        if (images.isEmpty()) { QPixmap image(":/explore/" + app.id + "-screenshot.png"); if (!image.isNull()) images.append(image); }
        screenshot->setImages(images); screenshot->setVisible(!images.isEmpty()); noScreenshot->hide();
        dots.clear();
        while (auto *item = galleryLayout->takeAt(0)) { delete item->widget(); delete item; }
        galleryLayout->addStretch();
        for (int i = 0; i < images.size(); ++i) {
            auto *dot = new CarouselDot; dot->playful = true; dot->setAccessibleName(QString("Screenshot %1 of %2").arg(i + 1).arg(images.size()));
            dot->setToolTip(dot->accessibleName()); dot->setChecked(i == 0); dots.append(dot); galleryLayout->addWidget(dot);
            connect(dot, &QAbstractButton::clicked, this, [this, i] { screenshot->select(i); });
        }
        galleryLayout->addStretch(); galleryControls->setVisible(images.size() > 1);
        const auto release = info.value("release").toObject();
        QString version = release.value("version").toString();
        const QDate date = QDate::fromString(release.value("date").toString(), Qt::ISODate);
        if (date.isValid()) version += (version.isEmpty() ? "" : " · ") + QLocale().toString(date, QLocale::ShortFormat);
        releaseVersion->setText(version); releaseVersion->setVisible(!version.isEmpty());
        const QString notes = release.value("notes").toString().trimmed();
        changelog->setText(notes); changelog->parentWidget()->setVisible(!notes.isEmpty());
        while (auto *item = addonLayout->takeAt(0)) { delete item->widget(); delete item; }
        addonLayout->addWidget(text("Add-ons", 16, true));
        const auto addons = info.value("addons").toArray();
        addonLayout->parentWidget()->setVisible(!addons.isEmpty());
        for (const auto &value : addons) {
            const auto addon = value.toObject();
            const DemoApp target{addon.value("name").toString(), addon.value("summary").toString(), app.category, addon.value("id").toString()};
            auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
            auto *open = new QCommandLinkButton(target.name, target.summary); open->setObjectName("openAddon");
            open->setIcon(QIcon::fromTheme("application-x-addon", QIcon::fromTheme("package-x-generic"))); open->setIconSize(QSize(28, 28));
            open->setMinimumHeight(56); open->setAccessibleName("Open " + target.name + " details");
            connect(open, &QAbstractButton::clicked, this, [this, target] { if (onOpenAddon) onOpenAddon(target); });
            auto *action = new QPushButton("Install"); action->setObjectName("installAddon"); action->setMinimumHeight(36);
            action->setIcon(QIcon::fromTheme("download"));
            if (operations) operations->bind(action, target.id, target.name);
            layout->addWidget(open, 1); layout->addWidget(action); addonLayout->addWidget(row);
        }
        while (auto *item = linksLayout->takeAt(0)) { delete item->widget(); delete item; }
        linksLayout->addWidget(text("Links", 16, true));
        const QMap<QString, QPair<QString, QString>> linkTypes = {
            {"homepage", {"Project website", "internet-web-browser"}}, {"help", {"Help", "help-browser"}},
            {"translate", {"Contribute translations", "preferences-desktop-locale"}}, {"bugtracker", {"Report an issue", "tools-report-bug"}},
            {"vcs-browser", {"Browse the source code", "code-context"}}, {"contribute", {"Contribute to the app", "applications-development"}},
            {"donation", {"Donate", "help-donate"}}, {"faq", {"Frequently asked questions", "help-contents"}},
            {"manifest", {"Manifest", "code-context"}}};
        int linkCount = 0;
        for (const auto &value : info.value("links").toArray()) {
            const auto link = value.toObject(); const QUrl url(link.value("url").toString());
            if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http")) continue;
            const auto type = linkTypes.value(link.value("type").toString(), {"Project link", "internet-web-browser"});
            auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
            auto *open = new QCommandLinkButton(type.first, url.toDisplayString()); open->setObjectName("appLink");
            open->setMinimumHeight(56); open->setIcon(QIcon::fromTheme(type.second)); open->setToolTip(url.toDisplayString());
            open->setAccessibleName(type.first + ": " + url.toDisplayString());
            auto *external = new QToolButton; external->setIcon(QIcon::fromTheme("window-new")); external->setAccessibleName("Open " + type.first); external->setToolTip("Open in browser");
            auto launch = [url] { QDesktopServices::openUrl(url); };
            connect(open, &QAbstractButton::clicked, this, launch); connect(external, &QToolButton::clicked, this, launch);
            layout->addWidget(open, 1); layout->addWidget(external); linksLayout->addWidget(row); ++linkCount;
        }
        linksLayout->parentWidget()->setVisible(linkCount > 0);
        while (auto *item = permissionLayout->takeAt(0)) { delete item->widget(); delete item; }
        const auto permissions = info.value("permissions").toArray();
        permissionBody->parentWidget()->setVisible(!permissions.isEmpty());
        const bool snapshot = !info.value("permissionSource").toString().isEmpty();
        permissionNote->setText(snapshot ? "From the installed package snapshot. Permissions may differ in other versions." : "Permission information is not available for this sample app.");
        for (const auto &value : permissions) {
            const auto permission = value.toObject();
            auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
            auto *symbol = new QLabel; symbol->setPixmap(QIcon::fromTheme(permission.value("icon").toString()).pixmap(24, 24)); symbol->setFixedWidth(30); layout->addWidget(symbol, 0, Qt::AlignTop);
            auto *copy = new QVBoxLayout; copy->setSpacing(5); copy->addWidget(text(permission.value("name").toString(), 0, true)); copy->addWidget(text(permission.value("value").toString())); layout->addLayout(copy, 1);
            permissionLayout->addWidget(row);
        }
        verticalScrollBar()->setValue(0);
    }
    void showGallery() { verticalScrollBar()->setValue(screenshot->mapTo(widget(), QPoint()).y() - 24); }
    void showChangelog() { if (!changelog->parentWidget()->isHidden()) verticalScrollBar()->setValue(findChild<QWidget *>("changelogPanel")->mapTo(widget(), QPoint()).y() - 20); }
    void showPermissions() { if (!permissionBody->parentWidget()->isHidden()) verticalScrollBar()->setValue(permissionsHeading->mapTo(widget(), QPoint()).y() - 20); }
    bool check() const {
        return !currentId.isEmpty() && !title->text().isEmpty() && (!changelog->text().isEmpty() || changelog->parentWidget()->isHidden())
            && data.contains(currentId) && (screenshot->images.isEmpty() == screenshot->isHidden())
            && dots.size() == screenshot->images.size();
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QScrollArea::resizeEvent(event);
        content->setFixedWidth(qMax(1, viewport()->width() - 56));
        updateDescription();
    }
private:
    QMetaObject::Connection installBinding;
    QVariantAnimation descriptionMotion;
    bool descriptionExpanded = false;
    QToolButton *descriptionToggle = nullptr;
    void updateDescription(bool animate = false) {
        const int fullHeight = qMax(0, description->heightForWidth(qMax(1, content->width())));
        const int collapsedHeight = QFontMetrics(description->font()).lineSpacing() * 6;
        descriptionMotion.stop();
        const int target = descriptionExpanded ? fullHeight : qMin(fullHeight, collapsedHeight);
        const int duration = animate ? desktopAnimationDuration(240) : 0;
        if (duration) { descriptionMotion.setDuration(duration); descriptionMotion.setStartValue(description->height()); descriptionMotion.setEndValue(target); descriptionMotion.start(); }
        else description->setFixedHeight(target);
        descriptionToggle->setVisible(fullHeight > collapsedHeight);
        descriptionToggle->setText(descriptionExpanded ? "Show less" : "Show more");
        descriptionToggle->setIcon(QIcon::fromTheme(descriptionExpanded ? "arrow-up" : "arrow-down"));
        descriptionToggle->setAccessibleName(descriptionExpanded ? "Collapse app description" : "Expand app description");
    }
    QWidget *content = nullptr;
    QJsonObject data;
    QWidget *galleryControls = nullptr;
    QHBoxLayout *galleryLayout = nullptr;
    QList<CarouselDot *> dots;
    QLabel *icon = nullptr, *summary = nullptr, *description = nullptr, *noScreenshot = nullptr;
    QLabel *permissionsHeading = nullptr, *permissionNote = nullptr;
    QLabel *releaseVersion = nullptr, *changelog = nullptr;
    QWidget *permissionBody = nullptr;
    QVBoxLayout *permissionLayout = nullptr;
    QVBoxLayout *addonLayout = nullptr, *linksLayout = nullptr;
    static QLabel *text(const QString &value, int points = 0, bool bold = false) {
        auto *label = new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
        QFont font = label->font(); if (points) font.setPointSize(points); font.setBold(bold); label->setFont(font); return label;
    }
};
