#pragma once
#include "applist.h"
#include "backend.h"
#include <QtWidgets>
#include <algorithm>
#include <functional>

static QIcon catalogIcon(const CatalogApp &app) {
    if (!app.iconPath.isEmpty()) { const QIcon icon(app.iconPath); if (!icon.isNull()) return icon; }
    // A box for bare packages; a generic app icon for apps whose publisher ships none.
    return QIcon::fromTheme(app.origin == MetadataOrigin::None ? "package-x-generic" : "applications-other");
}

// "Repo · Flatpak · AUR": which kinds of source provide the entry, in install-preference order.
static QString sourceSummary(const CatalogApp &app) {
    QStringList names;
    for (const auto &package : app.packages) {
        const QString name = package.source == Source::Repo ? "Repo" : package.source == Source::Flatpak ? "Flatpak" : "AUR";
        if (!names.contains(name)) names.append(name);
    }
    return names.join(" · ");
}

static QColor categoryColor(const QString &category) {
    if (category == "Graphics & Photography") return QColor("#674296");
    if (category == "Games") return QColor("#a24465");
    if (category == "Productivity") return QColor("#315c91");
    if (category == "Developer Tools") return QColor("#356b58");
    if (category == "Networking") return QColor("#8d5134");
    if (category == "Audio & Video") return QColor("#744d79");
    if (category == "Education & Science") return QColor("#526733");
    if (category == "System") return QColor("#486578");
    return QColor("#5c567e");
}

class ExploreTile : public QAbstractButton {
public:
    CatalogApp app;
    QString group;  // Explore category group; picks the tile's tint
    bool hero = false, spotlight = false, artworkBelow = false;
    QPixmap artwork;  // First publisher screenshot, once downloaded
    QPushButton *quickInstall = nullptr;
    explicit ExploreTile(const CatalogApp &value, bool featured = false, QWidget *parent = nullptr)
        : QAbstractButton(parent), app(value), group(categoryGroup(value.categories)), hero(featured) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("Open " + app.name);
        setToolTip(app.name + " — " + app.summary);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void bindQuickInstall(PreviewOperations *operations) {
        if (!operations) return;
        if (!quickInstall) {
            quickInstall = new QPushButton(QIcon::fromTheme("download"), "Install", this);
            quickInstall->setObjectName("tileQuickInstall"); quickInstall->setFixedSize(130, 36);
            quickInstall->setAccessibleName("Quick install " + app.name); quickInstall->show();
        }
        if (installBinding) disconnect(installBinding);
        installBinding = operations->bind(quickInstall, app.key, app.name);
        quickInstall->setAccessibleName("Quick install " + app.name);
        placeQuickInstall();
    }
    int heightForWidth(int width) const override {
        if (hero) return width < 650 ? 220 : 246;
        QFont body = font();
        const int textWidth = !spotlight && hasArtwork() && width >= 340 ? int(width * .54) : width;
        return (spotlight ? width / 2 + 110 : 110) + QFontMetrics(body).boundingRect(QRect(0, 0, qMax(80, textWidth - 28), 500), Qt::TextWordWrap, app.summary).height();
    }
    QSize sizeHint() const override { return QSize(190, heightForWidth(190)); }
    // Layout decisions use the screenshot list, so cards keep their place while images download.
    bool hasArtwork() const { return !app.screenshots.isEmpty(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QAbstractButton::resizeEvent(event); placeQuickInstall(); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape; shape.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
        p.setClipPath(shape);
        p.fillRect(rect(), palette().color(QPalette::Button));
        QColor wash = categoryColor(group); wash.setAlpha(hero ? 48 : 22); p.fillRect(rect(), wash);
        const QIcon icon = catalogIcon(app);
        const QColor ink = palette().color(QPalette::WindowText);
        p.setPen(ink);
        if (hero) {
            const int split = width() * 46 / 100;
            const QPixmap &art = artwork;
            const QRect picture(split, 0, width() - split, height());
            if (!art.isNull()) {
                const auto scaled = art.scaled(picture.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                p.drawPixmap(picture, scaled, QRect((scaled.width() - picture.width()) / 2, (scaled.height() - picture.height()) / 2, picture.width(), picture.height()));
                const QColor base = palette().color(QPalette::Button);
                const int alpha = wash.alpha(), inverse = 255 - alpha;
                const QColor surface((base.red() * inverse + wash.red() * alpha) / 255,
                    (base.green() * inverse + wash.green() * alpha) / 255,
                    (base.blue() * inverse + wash.blue() * alpha) / 255);
                const QColor transparent(surface.red(), surface.green(), surface.blue(), 0);
                QRadialGradient fade(QPointF(picture.left(), -height() * .12), picture.width() * 1.15);
                fade.setColorAt(0, surface); fade.setColorAt(.28, surface); fade.setColorAt(1, transparent);
                p.fillRect(picture, fade);
                // Keep the vertical join continuous while the radial wash softens the top.
                QLinearGradient join(picture.left(), 0, picture.left() + picture.width() * .32, 0);
                join.setColorAt(0, surface); join.setColorAt(1, transparent);
                p.fillRect(QRectF(picture.left(), 0, picture.width() * .32, height()), join);
            }
            p.drawPixmap(24, 24, icon.pixmap(56, 56));
            QFont title = font(); title.setPointSize(width() < 650 ? 18 : 22); title.setBold(true); p.setFont(title);
            p.drawText(QRect(96, 24, split - 114, 60), Qt::AlignVCenter | Qt::TextWordWrap, app.name);
            QFont copy = font(); copy.setPointSize(13); p.setFont(copy);
            p.drawText(QRect(24, 106, split - 48, height() - 145), Qt::TextWordWrap, app.summary);
        } else {
            const bool wide = !spotlight && hasArtwork() && width() >= 340;
            const int textWidth = wide ? int(width() * .54) : width();
            if (wide && !artwork.isNull()) {
                const QRect picture(textWidth, 0, width() - textWidth, height());
                const auto scaled = artwork.scaled(picture.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                p.drawPixmap(picture, scaled, QRect((scaled.width() - picture.width()) / 2, (scaled.height() - picture.height()) / 2, picture.width(), picture.height()));
                const QColor base = palette().color(QPalette::Button);
                const int alpha = wash.alpha(), inverse = 255 - alpha;
                const QColor surface((base.red() * inverse + wash.red() * alpha) / 255,
                    (base.green() * inverse + wash.green() * alpha) / 255, (base.blue() * inverse + wash.blue() * alpha) / 255);
                QLinearGradient fade(picture.left(), 0, picture.left() + picture.width() * .4, 0);
                fade.setColorAt(0, surface); fade.setColorAt(1, QColor(surface.red(), surface.green(), surface.blue(), 0)); p.fillRect(picture, fade);
            }
            int top = 0;
            if (spotlight) {
                const int copyHeight = 110 + QFontMetrics(font()).boundingRect(QRect(0, 0, qMax(80, width() - 28), 500), Qt::TextWordWrap, app.summary).height();
                const int imageHeight = qMax(0, height() - copyHeight);
                top = artworkBelow ? 0 : imageHeight;
                if (imageHeight > 0 && !artwork.isNull()) {
                    const auto scaled = artwork.scaled(QSize(width(), imageHeight), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    p.drawPixmap(QRect(0, artworkBelow ? copyHeight : 0, width(), imageHeight), scaled, QRect((scaled.width() - width()) / 2, (scaled.height() - imageHeight) / 2, width(), imageHeight));
                }
            }
            p.drawPixmap(14, top + 14, icon.pixmap(44, 44));
            QFont title = font(); title.setBold(true); p.setFont(title);
            p.drawText(QRect(14, top + 70, textWidth - 28, 24), Qt::AlignLeft | Qt::AlignVCenter, app.name);
            p.setFont(font());
            p.drawText(QRect(14, top + 100, textWidth - 28, height() - top - 106), Qt::TextWordWrap, app.summary);
        }
        if (hasFocus() && keyboardFocus) {
            p.setClipping(false); p.setBrush(Qt::NoBrush);
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 10, 10);
        }
    }
    void focusInEvent(QFocusEvent *event) override {
        keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
        QAbstractButton::focusInEvent(event); update();
    }
private:
    QMetaObject::Connection installBinding;
    void placeQuickInstall() {
        if (quickInstall) quickInstall->move(qMax(14, width() - quickInstall->width() - (hero ? 24 : 14)), hero ? qMax(14, height() - quickInstall->height() - 24) : 14);
    }
    bool keyboardFocus = false;
};

// Cards survive searches and resizes; only their geometry and visibility change.
class ExploreMasonry : public QWidget {
public:
    QList<ExploreTile *> tiles, order;
    explicit ExploreMasonry(QWidget *parent = nullptr) : QWidget(parent) {}
    void arrange() {
        QList<ExploreTile *> visible;
        for (auto *tile : order.isEmpty() ? tiles : order) if (!tile->isHidden()) visible.append(tile);
        if (visible.isEmpty()) { setFixedHeight(1); return; }
        const int count = qMin(int(visible.size()), qBound(2, width() / 185, 5));
        const int gap = 12;
        for (auto *tile : visible) tile->spotlight = false;
        int offset = 0, y = 0, band = 0;
        // Each band is fully partitioned into one image card and paired small cards.
        // ponytail: sections hold a handful of cards; virtualize if a section ever shows hundreds.
        while (offset < visible.size()) {
            const int span = count >= 4 && band % 2 == 0 ? 2 : 1;
            int size = qMin(int(visible.size()) - offset, 2 * (count - span) + 1);
            if (size > 1 && size % 2 == 0) --size;
            int chosen = -1;
            for (int i = 0; i < size; ++i)
                if (visible[offset + i]->hasArtwork()) { chosen = i; break; }
            if (size < 3) chosen = -1;  // An image card needs a pair of cards beside it, never a band of its own
            if (chosen < 0) size = qMin(count, int(visible.size()) - offset);
            QList<ExploreTile *> group;
            for (int i = 0; i < size; ++i) group.append(visible[offset + i]);
            ExploreTile *large = chosen < 0 ? nullptr : group.takeAt(chosen);
            const int pairs = large ? group.size() / 2 : group.size();
            const int columns = pairs + (large ? span : 0);
            const int columnWidth = (width() - gap * (columns - 1)) / columns;
            int bandHeight = 0;
            if (large) {
                large->spotlight = true;
                bandHeight = large->heightForWidth(columnWidth * span + gap * (span - 1));
            }
            for (int i = 0; i < group.size(); i += large ? 2 : 1) {
                int height = group[i]->heightForWidth(columnWidth);
                if (large) height += gap + group[i + 1]->heightForWidth(columnWidth);
                bandHeight = qMax(bandHeight, height);
            }
            const int largeLane = pairs ? band % (pairs + 1) : 0;
            int column = 0, next = 0;
            for (int lane = 0; lane < pairs + (large ? 1 : 0); ++lane) {
                const int laneSpan = large && lane == largeLane ? span : 1;
                const int x = column * (columnWidth + gap);
                const int right = column + laneSpan;
                const int cardWidth = right == columns ? width() - x : columnWidth * laneSpan + gap * (laneSpan - 1);
                if (large && lane == largeLane) large->setGeometry(x, y, cardWidth, bandHeight);
                else if (large) {
                    auto *top = group[next++], *bottom = group[next++];
                    const int topMinimum = top->heightForWidth(cardWidth), bottomMinimum = bottom->heightForWidth(cardWidth);
                    const int extra = bandHeight - gap - topMinimum - bottomMinimum;
                    const int topHeight = topMinimum + extra / 2;
                    top->setGeometry(x, y, cardWidth, topHeight);
                    bottom->setGeometry(x, y + topHeight + gap, cardWidth, bandHeight - topHeight - gap);
                } else group[next++]->setGeometry(x, y, cardWidth, bandHeight);
                column = right;
            }
            offset += size; y += bandHeight + gap; ++band;
        }
        setFixedHeight(qMax(1, y - gap));
    }
protected:
    void resizeEvent(QResizeEvent *) override { arrange(); }
};

static int desktopAnimationDuration(int milliseconds) {
    QSettings desktop(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, "kdeglobals"), QSettings::IniFormat);
    return qRound(milliseconds * qBound(0., desktop.value("KDE/AnimationDurationFactor", 1.).toDouble(), 10.));
}

class FeaturedCarousel : public QStackedWidget {
public:
    explicit FeaturedCarousel(QWidget *parent = nullptr) : QStackedWidget(parent) {
        overlay = new QLabel(this); overlay->setObjectName("featuredTransition"); overlay->hide();
        animation.setObjectName("featuredAnimation"); animation.setParent(this);
        QEasingCurve spring(QEasingCurve::OutBack); spring.setOvershoot(.65); animation.setEasingCurve(spring);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            if (size().isEmpty()) return;
            QPixmap frame(size() * devicePixelRatioF()); frame.setDevicePixelRatio(devicePixelRatioF()); frame.fill(palette().color(QPalette::Window));
            QPainter painter(&frame); painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath clip; clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10); painter.setClipPath(clip);
            const int offset = qRound(value.toReal() * width());
            painter.drawPixmap(-offset, 0, outgoing); painter.drawPixmap(direction * width() - offset, 0, incoming);
            painter.end(); overlay->setPixmap(frame);
        });
        connect(&animation, &QVariantAnimation::finished, this, [this] { finish(); });
    }
    void select(int index) {
        if (index == currentIndex()) return;
        direction = index > currentIndex() ? 1 : -1;
        const int duration = desktopAnimationDuration(420);
        animation.stop();
        if (!isVisible() || duration == 0) { finish(); setCurrentIndex(index); return; }
        outgoing = grab(); overlay->hide(); setCurrentIndex(index); incoming = grab();
        overlay->setGeometry(rect()); overlay->setPixmap(outgoing); overlay->show(); overlay->raise();
        animation.setDuration(duration); animation.setStartValue(0.); animation.setEndValue(qreal(direction)); animation.start();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { finish(); QStackedWidget::resizeEvent(event); }
    void hideEvent(QHideEvent *event) override { finish(); QStackedWidget::hideEvent(event); }
private:
    QVariantAnimation animation;
    QLabel *overlay = nullptr;
    QPixmap outgoing, incoming;
    int direction = 1;
    void finish() { animation.stop(); overlay->hide(); overlay->clear(); outgoing = {}; incoming = {}; }
};

class CarouselDot : public QAbstractButton {
public:
    bool playful = false;
    explicit CarouselDot(QWidget *parent = nullptr) : QAbstractButton(parent) {
        animation.setParent(this); animation.setObjectName("indicatorAnimation");
        QEasingCurve spring(QEasingCurve::OutBack); spring.setOvershoot(1.); animation.setEasingCurve(spring);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            length = value.toReal(); setFixedWidth(qCeil(length) + 4); update();
        });
        connect(this, &QAbstractButton::toggled, this, [this] { updateBar(); });
        setCheckable(true); setFixedSize(28, 36);
        setAutoFillBackground(false); setAttribute(Qt::WA_StyledBackground, false);
        setStyleSheet("background:transparent;border:none;"); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
    }
    void updateBar() {
        animation.stop(); const qreal target = isChecked() ? 80. : 24.;
        const int duration = playful && isVisible() ? desktopAnimationDuration(280) : 0;
        if (duration) { animation.setDuration(duration); animation.setStartValue(length); animation.setEndValue(target); animation.start(); }
        else { length = target; setFixedWidth(target + 4); update(); }
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        QColor ink = palette().color(isChecked() ? QPalette::Highlight : QPalette::WindowText);
        if (!isChecked()) ink.setAlpha(130);
        painter.setPen(Qt::NoPen); painter.setBrush(ink);
        painter.drawRoundedRect(QRectF((width() - length) / 2., (height() - 7) / 2., length, 7), 3.5, 3.5);
        if (hasFocus() && keyboardFocus) {
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
            painter.drawEllipse(QPointF(width() / 2., height() / 2.), 12, 12);
        }
    }
    void focusInEvent(QFocusEvent *event) override {
        keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
        QAbstractButton::focusInEvent(event); update();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { click(); event->accept(); }
        else QAbstractButton::keyPressEvent(event);
    }
private:
    QVariantAnimation animation;
    qreal length = 24;
    bool keyboardFocus = false;
};

class ExplorePage : public QScrollArea {
public:
    Backend *backend = nullptr;
    QComboBox *source = nullptr, *sort = nullptr;
    QLineEdit *search = nullptr;
    QToolButton *filters = nullptr;
    QWidget *filterPanel = nullptr;
    struct Section { QLabel *heading = nullptr; ExploreMasonry *masonry = nullptr; };
    QList<Section> sections;  // Trending on Flathub, Popular in Arch, Popular on AUR
    AppList *results = nullptr, *others = nullptr;
    PreviewOperations *operations = nullptr;
    QPushButton *loadMore = nullptr, *moreOthers = nullptr;
    QLabel *notice = nullptr;  // Setup problems the user can fix, with the exact command
    QLabel *offlineNote = nullptr, *countLabel = nullptr, *othersLabel = nullptr, *empty = nullptr;
    FeaturedCarousel *carousel = nullptr;
    QTimer searchDelay;
    std::function<void(const CatalogApp &)> onOpen;
    static constexpr int pageSize = 50;

    explicit ExplorePage(Backend *source, QWidget *parent = nullptr) : QScrollArea(parent), backend(source) {
        setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget; setWidget(body);
        auto *layout = new QVBoxLayout(body); layout->setContentsMargins(20, 16, 20, 24); layout->setSpacing(16);
        layout->addWidget(text("Explore", 23, true));
        notice = text(""); notice->setObjectName("exploreNotice"); notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
        notice->hide(); layout->addWidget(notice);
        buildSearch(layout);
        buildFilters(layout);
        buildCarousel(layout);
        offlineNote = text("Offline: showing saved suggestions."); offlineNote->setObjectName("offlineNote"); offlineNote->hide();
        layout->addWidget(offlineNote);
        for (const QString &title : {QString("Trending on Flathub"), QString("Popular in Arch"), QString("Popular on AUR")}) {
            Section section{text(title, 12, true), new ExploreMasonry};
            section.masonry->setObjectName("exploreSection"); layout->addWidget(section.heading); layout->addWidget(section.masonry);
            sections.append(section);
        }
        buildHoverPreview();
        countLabel = text("", 12, true); countLabel->setObjectName("resultsHeading"); layout->addWidget(countLabel);
        results = new AppList; results->setObjectName("exploreResults"); results->setAccessibleName("Apps"); layout->addWidget(results);
        loadMore = new QPushButton("Load more"); loadMore->setObjectName("loadMore"); loadMore->setMinimumHeight(32);
        layout->addWidget(loadMore, 0, Qt::AlignHCenter);
        othersLabel = text("", 12, true); othersLabel->setObjectName("otherPackagesHeading");
        othersLabel->setToolTip("Packages without app metadata: libraries, command-line tools, and most AUR packages.");
        layout->addWidget(othersLabel);
        others = new AppList; others->setObjectName("otherPackages"); others->setAccessibleName("Other packages"); layout->addWidget(others);
        moreOthers = new QPushButton("Show more packages"); moreOthers->setObjectName("moreOthers"); moreOthers->setMinimumHeight(32);
        layout->addWidget(moreOthers, 0, Qt::AlignHCenter);
        empty = text("Loading apps…"); empty->setObjectName("exploreEmpty"); layout->addWidget(empty);
        layout->addStretch();
        connect(loadMore, &QPushButton::clicked, this, [this] { showMore(results, loadMore, resultItems, resultShown); });
        connect(moreOthers, &QPushButton::clicked, this, [this] { showMore(others, moreOthers, otherItems, otherShown); });
        for (auto *list : {results, others}) {
            connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { openPreview(entry(item->data(Qt::UserRole).toInt())); });
            connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { openPreview(entry(item->data(Qt::UserRole).toInt())); });
        }
        applyFilters();
    }
    void setOperations(PreviewOperations *value) {
        operations = value;
        for (int i = 0; i < carousel->count(); ++i) bindTile(static_cast<ExploreTile *>(carousel->widget(i)));
        applyFilters();
    }
    // Called when the catalogue or the rankings arrive.
    void setCatalog() {
        const auto &apps = backend->apps;
        scores.clear(); groups.clear(); extra.clear();
        scores.reserve(apps.size()); groups.reserve(apps.size());
        for (const auto &app : apps) { scores.append(appScore(app, backend->rankings)); groups.append(categoryGroup(app.categories)); }
        QList<int> named;
        for (int i = 0; i < apps.size(); ++i) if (apps[i].origin != MetadataOrigin::None) named.append(i);
        std::sort(named.begin(), named.end(), [this](int a, int b) { return scores[a] > scores[b]; });
        QStringList names;
        for (int index : named) if (!names.contains(apps[index].name)) names.append(apps[index].name);
        suggestions.setStringList(names);
        rebuildSuggestions();
        applyFilters();
    }
    const CatalogApp &entry(int index) const { return index < backend->apps.size() ? backend->apps[index] : extra[index - backend->apps.size()]; }
    void advance(int step) {
        const int count = carousel->count();
        if (!count) return;
        carousel->select(((carousel->currentIndex() + step) % count + count) % count);
        dots[carousel->currentIndex()]->setChecked(true);
        updateAutoplay();
    }
    void openPreview(const CatalogApp &app) { if (onOpen) onOpen(app); }
    void showCollection() { verticalScrollBar()->setValue(sections.first().heading->mapTo(widget(), QPoint()).y() - 16); }
    void showDirectory() { verticalScrollBar()->setValue(countLabel->mapTo(widget(), QPoint()).y() - 16); }
    QList<ExploreTile *> sectionTiles() const {
        QList<ExploreTile *> tiles;
        for (const auto &section : sections) tiles.append(section.masonry->tiles);
        return tiles;
    }
    void applyFilters() {
        searchDelay.stop();
        hideHoverPreview();
        const auto &apps = backend->apps;
        const QString query = search->text().trimmed();
        const QString wanted = source->currentText();
        auto sourceMatch = [&wanted](const CatalogApp &app) {
            if (wanted == "All sources") return true;
            const Source kind = wanted == "Flatpak" ? Source::Flatpak : wanted == "AUR" ? Source::Aur : Source::Repo;
            for (const auto &package : app.packages) if (package.source == kind) return true;
            return false;
        };
        QList<int> candidates;
        if (query.isEmpty()) {
            for (int i = 0; i < apps.size(); ++i) if (apps[i].origin != MetadataOrigin::None) candidates.append(i);
            std::sort(candidates.begin(), candidates.end(), [this](int a, int b) { return scores[a] > scores[b]; });
        } else candidates = rankSearch(apps, query, scores);
        if (sort->currentIndex() > 0) {
            std::stable_sort(candidates.begin(), candidates.end(), [&apps](int a, int b) { return apps[a].name.localeAwareCompare(apps[b].name) < 0; });
            if (sort->currentIndex() == 2) std::reverse(candidates.begin(), candidates.end());
        }
        resultItems.clear(); otherItems.clear(); extra.clear();
        for (int index : candidates) {
            const auto &app = apps[index];
            if (!sourceMatch(app) || (!category.isEmpty() && groups[index] != category)) continue;
            (app.origin == MetadataOrigin::None ? otherItems : resultItems).append(index);
        }
        // Zero-vote AUR packages stay out of the catalogue; an exact name still finds them.
        if (!query.isEmpty() && category.isEmpty() && (wanted == "All sources" || wanted == "AUR")) {
            bool listed = false;
            for (int index : resultItems + otherItems) for (const auto &package : apps[index].packages) listed |= package.name == query;
            for (const auto &aur : backend->aur) if (!listed && !aur.detailed() && aur.name == query) {
                CatalogApp app; app.key = "aur:" + aur.name; app.name = aur.name;
                app.summary = "AUR package without votes or description";
                app.packages.append({Source::Aur, aur.name, "aur", aur.version});
                extra.append(app); otherItems.prepend(apps.size() + extra.size() - 1);
            }
        }
        resultShown = otherShown = 0;
        results->clear(); others->clear();
        showMore(results, loadMore, resultItems, resultShown);
        showMore(others, moreOthers, otherItems, otherShown);
        const bool ready = !apps.isEmpty();
        const bool browse = ready && query.isEmpty() && category.isEmpty() && wanted == "All sources";
        carousel->setVisible(browse && carousel->count()); carouselDots->setVisible(browse && carousel->count() > 1);
        offlineNote->setVisible(browse && backend->rankings.offline);
        for (const auto &section : sections) {
            section.heading->setVisible(browse && !section.masonry->tiles.isEmpty());
            section.masonry->setVisible(browse && !section.masonry->tiles.isEmpty());
        }
        const QString order = sort->currentIndex() == 0 ? (query.isEmpty() ? "most popular first" : "best match first") : sort->currentText();
        QString heading = query.isEmpty() ? (category.isEmpty() ? "All apps" : category) : "Apps";
        countLabel->setText(QString("%1 · %2 · %3").arg(heading, QLocale().toString(resultItems.size()), order));
        countLabel->setVisible(!resultItems.isEmpty());
        othersLabel->setText(QString("Other packages · %1").arg(QLocale().toString(otherItems.size())));
        othersLabel->setVisible(!otherItems.isEmpty());
        if (!ready) empty->setText("Loading apps…");
        else if (query.isEmpty()) empty->setText("No apps in this category from this source. Choose All sources or clear the filters.");
        else empty->setText(QString("Nothing matches “%1”. Check the spelling, or clear the filters.").arg(query));
        empty->setVisible(resultItems.isEmpty() && otherItems.isEmpty());
    }
    bool check() {
        const auto &apps = backend->apps;
        auto names = [this](AppList *list) {
            QStringList values;
            for (int i = 0; i < list->count(); ++i) values.append(entry(list->item(i)->data(Qt::UserRole).toInt()).name);
            return values;
        };
        bool passed = apps.size() == 5 && names(results) == QStringList{"Firefox", "GIMP", "Krita"} && others->count() == 0;
        passed &= carousel->count() == 3 && !carousel->isHidden() && carousel->height() == (viewport()->width() < 650 ? 220 : 246);
        QStringList sectionNames;
        for (const auto &section : sections) {
            QStringList tiles;
            for (auto *tile : section.masonry->tiles) tiles.append(tile->app.name);
            sectionNames.append(tiles.join(','));
        }
        passed &= sectionNames == QStringList{"GIMP,Krita", "Firefox", "pacseek"};
        passed &= !filterPanel->isVisibleTo(this) && !filters->isChecked();
        filters->click(); filterMotion.setCurrentTime(filterMotion.duration());
        const int filterHeight = filterPanel->height(); filters->click();
        if (filterMotion.state() == QAbstractAnimation::Running) {
            filterMotion.setCurrentTime(filterMotion.duration() / 2);
            passed &= filterPanel->height() > 0 && filterPanel->height() < filterHeight;
            filterMotion.setCurrentTime(filterMotion.duration());
        }
        passed &= filterPanel->isHidden();
        dots[1]->click(); passed &= carousel->currentIndex() == 1 && dots[1]->isChecked() && !dots[0]->isChecked();
        auto find = [this](const QString &query) { search->setText(query); applyFilters(); };
        find("krita"); passed &= names(results) == QStringList{"Krita"} && others->count() == 0 && carousel->isHidden();
        find("paint"); passed &= names(results) == QStringList{"Krita"};  // keyword
        find("python"); passed &= results->count() == 0 && names(others) == QStringList{"python-requests"} && !othersLabel->isHidden();
        find("woof-quiet"); passed &= names(others) == QStringList{"woof-quiet"};
        find("no such app"); passed &= results->count() == 0 && others->count() == 0 && !empty->isHidden();
        find(""); source->setCurrentText("AUR"); applyFilters(); passed &= names(results) == QStringList{"Krita"} && carousel->isHidden();
        source->setCurrentIndex(0); sort->setCurrentIndex(1); applyFilters(); passed &= names(results) == QStringList{"Firefox", "GIMP", "Krita"};
        sort->setCurrentIndex(2); applyFilters(); passed &= names(results) == QStringList{"Krita", "GIMP", "Firefox"};
        sort->setCurrentIndex(0); applyFilters();
        passed &= categoryButtons.size() == 9;
        const QList<int> expected{0, 0, 0, 0, 2, 1, 0, 0, 0};
        for (int i = 0; i < categoryButtons.size(); ++i) {
            categoryButtons[i]->click(); passed &= results->count() == expected[i];
            categoryButtons[i]->click();
        }
        passed &= results->count() == 3 && !carousel->isHidden() && suggestions.stringList().first() == "Firefox";
        if (!passed) qWarning() << "Explore check:" << names(results) << names(others) << sectionNames << carousel->count();
        qobject_cast<QAbstractButton *>(carousel->currentWidget())->click();  // Leaves the featured app's details open
        return passed;
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (hoverPreview && (object == hoverPreview || object == hoverPreview->quickInstall)) {
            if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
                dismissDelay.stop(); hoverFade.stop(); hoverOpacity->setOpacity(1.);
            }
            if ((event->type() == QEvent::Leave && object == hoverPreview) || (event->type() == QEvent::FocusOut && !hoverPreview->underMouse())) dismissDelay.start();
        }
        for (const auto &section : sections) for (auto *tile : section.masonry->tiles) if (object == tile) {
            if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
                hideHoverPreview(); hoverSource = tile; hoverDelay.start();
            }
            if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut) { hoverDelay.stop(); dismissDelay.start(); }
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && hoverPreview && hoverPreview->isVisible()) { hideHoverPreview(); return true; }
        if (object == carousel || object == carouselDots) {
            if (event->type() == QEvent::Enter) hovered.insert(object);
            if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) hovered.remove(object);
        }
        auto *control = qobject_cast<QWidget *>(object);
        bool rotationControl = object == carousel || object == carouselDots || (control && carousel->isAncestorOf(control));
        for (auto *dot : dots) rotationControl |= object == dot;
        if (rotationControl) {
            if (event->type() == QEvent::Wheel) {
                auto *wheel = static_cast<QWheelEvent *>(event);
                const bool pixels = wheel->angleDelta().isNull();
                const QPoint delta = pixels ? wheel->pixelDelta() : wheel->angleDelta();
                const int distance = qAbs(delta.x()) > qAbs(delta.y()) ? delta.x() : delta.y();
                const int threshold = pixels ? 60 : 120;
                if (wheel->phase() == Qt::ScrollBegin || wheelPixels != pixels) wheelDistance = 0;
                wheelPixels = pixels; wheelDistance += distance;
                const int steps = wheelDistance / threshold;
                if (steps) { wheelDistance %= threshold; advance(-steps); }
                if (wheel->phase() == Qt::ScrollEnd) wheelDistance = 0;
                wheel->accept(); return true;
            }
            if (event->type() == QEvent::FocusIn) {
                const auto reason = static_cast<QFocusEvent *>(event)->reason();
                keyboardCarouselFocus = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason ? control : nullptr;
            }
            if (event->type() == QEvent::FocusOut && keyboardCarouselFocus == control) keyboardCarouselFocus.clear();
            if (event->type() == QEvent::Hide) autoplay.stop();
            else if (event->type() == QEvent::Enter || event->type() == QEvent::Leave || event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut || event->type() == QEvent::Show)
                QTimer::singleShot(0, this, [this] { updateAutoplay(); });
        }
        return QScrollArea::eventFilter(object, event);
    }
    void hideEvent(QHideEvent *event) override { hideHoverPreview(); autoplay.stop(); hovered.clear(); QScrollArea::hideEvent(event); }
    void showEvent(QShowEvent *event) override { QScrollArea::showEvent(event); QTimer::singleShot(0, this, [this] { updateAutoplay(); }); }
    void resizeEvent(QResizeEvent *event) override {
        QScrollArea::resizeEvent(event);
        hideHoverPreview();
        carousel->setFixedHeight(viewport()->width() < 650 ? 220 : 246);
        QTimer::singleShot(0, this, [this] { for (const auto &section : sections) section.masonry->arrange(); });
    }
private:
    QList<double> scores;
    QStringList groups;      // Category group per catalogue entry
    QList<CatalogApp> extra; // Exact-name AUR matches outside the catalogue; indexes continue after backend->apps
    QList<int> resultItems, otherItems;
    int resultShown = 0, otherShown = 0;
    QStringListModel suggestions;
    QString category;
    QList<QPushButton *> categoryButtons;
    QVariantAnimation filterMotion, hoverMotion, hoverFade;
    QGraphicsOpacityEffect *hoverOpacity = nullptr;
    QTimer hoverDelay, dismissDelay, autoplay;
    ExploreTile *hoverPreview = nullptr;
    QPointer<ExploreTile> hoverSource;
    QWidget *carouselDots = nullptr;
    QHBoxLayout *dotRow = nullptr;
    QButtonGroup *dotGroup = nullptr;
    QList<CarouselDot *> dots;
    QSet<QObject *> hovered;
    QPointer<QWidget> keyboardCarouselFocus;
    int wheelDistance = 0;
    bool wheelPixels = false;
    static QLabel *text(const QString &value, int size = 0, bool bold = false) {
        auto *item = new QLabel(value); item->setTextFormat(Qt::PlainText); item->setWordWrap(true);
        QFont font = item->font(); if (size) font.setPointSize(size); font.setBold(bold); item->setFont(font); return item;
    }
    void buildSearch(QVBoxLayout *layout) {
        auto *searchRow = new QHBoxLayout;
        search = new QLineEdit; search->setObjectName("appSearch"); search->setPlaceholderText("Search apps and packages");
        search->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition); search->setClearButtonEnabled(true);
        search->setMinimumHeight(38); search->setAccessibleName("Search apps and packages"); searchRow->addWidget(search, 1);
        auto *completer = new QCompleter(&suggestions, this);
        completer->setCaseSensitivity(Qt::CaseInsensitive); completer->setFilterMode(Qt::MatchContains); completer->setMaxVisibleItems(8);
        search->setCompleter(completer);
        connect(completer, qOverload<const QString &>(&QCompleter::activated), this, [this] { applyFilters(); });
        // Wait for a pause in typing: one search per word instead of one per key.
        searchDelay.setParent(this); searchDelay.setSingleShot(true); searchDelay.setInterval(150);
        connect(&searchDelay, &QTimer::timeout, this, [this] { applyFilters(); });
        connect(search, &QLineEdit::textChanged, &searchDelay, qOverload<>(&QTimer::start));
        connect(search, &QLineEdit::returnPressed, this, [this] { applyFilters(); });
        filters = new QToolButton; filters->setIcon(QIcon::fromTheme("view-filter")); filters->setText("Filters"); filters->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); filters->setCheckable(true);
        filters->setMinimumHeight(38); filters->setAccessibleName("Show or hide filters and categories"); searchRow->addWidget(filters);
        layout->addLayout(searchRow);
    }
    void buildFilters(QVBoxLayout *layout) {
        filterPanel = new QWidget; auto *filterLayout = new QVBoxLayout(filterPanel); filterLayout->setContentsMargins(0, 0, 0, 0); filterLayout->setSpacing(12);
        auto *options = new QHBoxLayout;
        source = new QComboBox; source->setObjectName("softwareSource"); source->addItems({"All sources", "Repositories", "Flatpak", "AUR"}); source->setAccessibleName("Software source");
        options->addWidget(text("Source")); options->addWidget(source);
        sort = new QComboBox; sort->setObjectName("appSort"); sort->addItems({"Recommended", "Name A–Z", "Name Z–A"}); sort->setAccessibleName("App sort order");
        options->addSpacing(16); options->addWidget(text("Sort")); options->addWidget(sort); options->addStretch();
        auto *reset = new QPushButton("Clear filters"); options->addWidget(reset); filterLayout->addLayout(options);
        auto *categories = new QGridLayout; categories->setSpacing(10);
        const QMap<QString, QString> icons{{"Audio & Video", "applications-multimedia"}, {"Developer Tools", "applications-development"}, {"Education & Science", "applications-science"}, {"Games", "applications-games"}, {"Graphics & Photography", "applications-graphics"}, {"Networking", "applications-internet"}, {"Productivity", "applications-office"}, {"System", "applications-system"}, {"Utilities", "applications-utilities"}};
        int i = 0;
        for (auto it = icons.begin(); it != icons.end(); ++it, ++i) {
            const QString name = it.key();
            QPixmap glyph = QIcon::fromTheme(it.value(), QIcon::fromTheme("applications-other")).pixmap(22, 22);
            if (!glyph.isNull()) { QPainter tint(&glyph); tint.setCompositionMode(QPainter::CompositionMode_SourceIn); tint.fillRect(glyph.rect(), Qt::white); }
            QIcon symbol(glyph); symbol.addPixmap(glyph, QIcon::Selected); symbol.addPixmap(glyph, QIcon::Active);
            QString caption = name;
            if (name == "Graphics & Photography" || name == "Education & Science") caption.replace(" & ", " &\n");
            caption.replace('&', "&&");
            auto *item = new QPushButton(symbol, caption); item->setAccessibleName(name); item->setIconSize(QSize(22, 22)); item->setCheckable(true); item->setMinimumHeight(58); item->setCursor(Qt::PointingHandCursor);
            item->setStyleSheet(QString("QPushButton {background:%1;color:white;border:2px solid transparent;border-radius:9px;font-weight:bold;padding:8px;} QPushButton:hover,QPushButton:focus,QPushButton:checked {border-color:white;}").arg(categoryColor(name).name()));
            categoryButtons.append(item); categories->addWidget(item, i / 3, i % 3);
            connect(item, &QPushButton::clicked, this, [this, name, item](bool checked) {
                for (auto *other : categoryButtons) if (other != item) other->setChecked(false);
                category = checked ? name : QString(); applyFilters();
            });
        }
        filterLayout->addLayout(categories); layout->addWidget(filterPanel);
        filterPanel->hide();
        filterMotion.setParent(this); filterMotion.setObjectName("filterMotion"); filterMotion.setEasingCurve(QEasingCurve::OutCubic);
        connect(&filterMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { filterPanel->setFixedHeight(value.toInt()); });
        connect(&filterMotion, &QVariantAnimation::finished, this, [this] {
            if (filters->isChecked()) { filterPanel->setMinimumHeight(0); filterPanel->setMaximumHeight(QWIDGETSIZE_MAX); }
            else filterPanel->hide();
        });
        connect(filters, &QToolButton::toggled, this, [this](bool expanded) {
            filterMotion.stop(); const int start = filterPanel->isHidden() ? 0 : filterPanel->height();
            const int target = expanded ? filterPanel->sizeHint().height() : 0;
            filterPanel->show(); const int duration = desktopAnimationDuration(240);
            if (!isVisible() || !duration) { filterPanel->setVisible(expanded); filterPanel->setMinimumHeight(0); filterPanel->setMaximumHeight(QWIDGETSIZE_MAX); return; }
            filterPanel->setFixedHeight(start); filterMotion.setDuration(duration);
            filterMotion.setStartValue(start); filterMotion.setEndValue(target); filterMotion.start();
        });
        connect(source, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
        connect(sort, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
        connect(reset, &QPushButton::clicked, this, [this] {
            category.clear(); for (auto *item : categoryButtons) item->setChecked(false);
            search->clear(); source->setCurrentIndex(0); sort->setCurrentIndex(0); applyFilters();
        });
    }
    void buildCarousel(QVBoxLayout *layout) {
        carousel = new FeaturedCarousel; carousel->setObjectName("featuredCarousel"); carousel->setFixedHeight(246);
        layout->addWidget(carousel);
        carouselDots = new QWidget; dotRow = new QHBoxLayout(carouselDots);
        dotRow->setContentsMargins(0, 0, 0, 0); dotRow->setSpacing(0); dotRow->addStretch(); dotRow->addStretch();
        dotGroup = new QButtonGroup(this);
        layout->addWidget(carouselDots);
        autoplay.setParent(this); autoplay.setObjectName("featuredAutoplay"); autoplay.setInterval(5000);
        connect(&autoplay, &QTimer::timeout, this, [this] { advance(1); });
        carousel->installEventFilter(this); carouselDots->installEventFilter(this);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateAutoplay(); });
    }
    void buildHoverPreview() {
        hoverPreview = new ExploreTile({}, false, viewport()); hoverPreview->setObjectName("mosaicHoverPreview"); hoverPreview->hide(); hoverPreview->setFocusPolicy(Qt::NoFocus);
        hoverPreview->installEventFilter(this);
        hoverOpacity = new QGraphicsOpacityEffect(hoverPreview); hoverOpacity->setOpacity(1.); hoverPreview->setGraphicsEffect(hoverOpacity);
        hoverFade.setParent(this); hoverFade.setObjectName("mosaicHoverFade"); hoverFade.setEasingCurve(QEasingCurve::OutCubic);
        connect(&hoverFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { hoverOpacity->setOpacity(value.toReal()); });
        connect(&hoverFade, &QVariantAnimation::finished, this, [this] { hideHoverPreview(); });
        hoverDelay.setSingleShot(true); hoverDelay.setInterval(350); hoverDelay.setParent(this); hoverDelay.setObjectName("mosaicHoverDelay");
        dismissDelay.setSingleShot(true); dismissDelay.setInterval(150); dismissDelay.setParent(this); dismissDelay.setObjectName("mosaicDismissDelay");
        connect(&hoverDelay, &QTimer::timeout, this, [this] { showHoverPreview(); });
        connect(&dismissDelay, &QTimer::timeout, this, [this] { fadeHoverPreview(); });
        hoverMotion.setParent(this); hoverMotion.setObjectName("mosaicHoverMotion"); hoverMotion.setEasingCurve(QEasingCurve::OutCubic);
        connect(&hoverMotion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { hoverPreview->setGeometry(value.toRect()); });
        connect(hoverPreview, &QAbstractButton::clicked, this, [this] { if (hoverSource) { const auto app = hoverSource->app; hideHoverPreview(); openPreview(app); } });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { hideHoverPreview(); });
    }
    void bindTile(ExploreTile *tile) {
        tile->bindQuickInstall(operations);
        if (tile->quickInstall) tile->quickInstall->installEventFilter(this);
    }
    void loadArtwork(ExploreTile *tile) {
        if (tile->app.screenshots.isEmpty()) return;
        backend->fetchImage(tile->app.screenshots.first(), tile, [tile](const QPixmap &image) { tile->artwork = image; tile->update(); });
    }
    // Featured slides and the three suggestion sections, from Flathub and pkgstats rankings.
    void rebuildSuggestions() {
        hideHoverPreview();
        const auto &apps = backend->apps;
        const auto &rankings = backend->rankings;
        QHash<QString, int> byFlatpak;
        for (int i = 0; i < apps.size(); ++i)
            if (apps[i].origin != MetadataOrigin::None)
                for (const auto &package : apps[i].packages) if (package.source == Source::Flatpak) byFlatpak.insert(package.name, i);
        auto ranked = [&](const QStringList &ids, int limit, bool needsScreenshot, QSet<int> *used) {
            QList<int> picks;
            for (const QString &id : ids) {
                const int index = byFlatpak.value(id, -1);
                if (index < 0 || (used && used->contains(index)) || (needsScreenshot && apps[index].screenshots.isEmpty())) continue;
                picks.append(index); if (used) used->insert(index);
                if (picks.size() == limit) break;
            }
            return picks;
        };
        QList<int> featured = ranked(rankings.popular, 5, true, nullptr);
        if (featured.isEmpty()) {  // Never downloaded rankings: the most popular apps with screenshots
            QList<int> named;
            for (int i = 0; i < apps.size(); ++i) if (apps[i].origin != MetadataOrigin::None && !apps[i].screenshots.isEmpty()) named.append(i);
            std::sort(named.begin(), named.end(), [this](int a, int b) { return scores[a] > scores[b]; });
            featured = named.mid(0, 5);
        }
        while (carousel->count()) { auto *slide = carousel->widget(0); carousel->removeWidget(slide); delete slide; }
        for (auto *dot : dots) delete dot;
        dots.clear();
        for (int index : featured) {
            auto *slide = new ExploreTile(apps[index], true); slide->installEventFilter(this); carousel->addWidget(slide);
            bindTile(slide); loadArtwork(slide);
            connect(slide, &QAbstractButton::clicked, this, [this, slide] { openPreview(slide->app); });
            auto *dot = new CarouselDot; dot->setObjectName(QString("featuredDot%1").arg(dots.size())); dot->playful = true; dot->installEventFilter(this);
            dot->setAccessibleName(QString("Show %1 featured slide, %2 of %3").arg(apps[index].name).arg(dots.size() + 1).arg(featured.size()));
            dot->setToolTip(apps[index].name); dotGroup->addButton(dot); dotRow->insertWidget(dotRow->count() - 1, dot);
            const int position = dots.size();
            connect(dot, &QAbstractButton::clicked, this, [this, position] { advance(position - carousel->currentIndex()); });
            dots.append(dot);
        }
        if (!dots.isEmpty()) dots.first()->setChecked(true);

        QSet<int> used;
        QList<QList<int>> picks{ranked(rankings.trending, 8, false, &used)};
        QList<int> arch;
        for (int i = 0; i < apps.size(); ++i) {
            // ponytail: pkgstats counts dependencies too (avahi, v4l-utils); requiring screenshots keeps their helper apps out.
            if (apps[i].origin == MetadataOrigin::None || used.contains(i) || apps[i].screenshots.isEmpty()) continue;
            for (const auto &package : apps[i].packages)
                if (package.source == Source::Repo && rankings.archPopularity.contains(package.name)) { arch.append(i); break; }
        }
        auto archScore = [&](int index) {
            double best = 0;
            for (const auto &package : apps[index].packages) best = qMax(best, rankings.archPopularity.value(package.name));
            return best;
        };
        std::sort(arch.begin(), arch.end(), [&](int a, int b) { return archScore(a) > archScore(b); });
        picks.append(arch.mid(0, 8));
        QList<int> aurOnly;
        for (int i = 0; i < apps.size(); ++i) {
            bool onlyAur = !apps[i].packages.isEmpty();
            for (const auto &package : apps[i].packages) onlyAur &= package.source == Source::Aur;
            if (onlyAur && !isLibraryName(apps[i].packages.first().name)) aurOnly.append(i);
        }
        std::sort(aurOnly.begin(), aurOnly.end(), [&apps](int a, int b) { return apps[a].packages.first().popularity > apps[b].packages.first().popularity; });
        picks.append(aurOnly.mid(0, 8));
        for (int s = 0; s < sections.size(); ++s) {
            auto *masonry = sections[s].masonry;
            qDeleteAll(masonry->tiles); masonry->tiles.clear(); masonry->order.clear();
            for (int index : picks[s]) {
                auto *tile = new ExploreTile(apps[index], false, masonry); masonry->tiles.append(tile);
                tile->installEventFilter(this); tile->show(); loadArtwork(tile);
                connect(tile, &QAbstractButton::clicked, this, [this, tile] { openPreview(tile->app); });
            }
            masonry->arrange();
        }
    }
    void showMore(AppList *list, QPushButton *more, const QList<int> &items, int &shown) {
        const int end = qMin(int(items.size()), shown + pageSize);
        for (; shown < end; ++shown) {
            const auto &app = entry(items[shown]);
            list->addApp(app.name, app.summary, catalogIcon(app), items[shown], sourceSummary(app), operations, app.key);
        }
        list->fitRows();
        more->setVisible(shown < items.size());
        more->setText(QString("%1 (%2 more)").arg(more == loadMore ? "Load more" : "Show more packages", QLocale().toString(items.size() - shown)));
    }
    void fadeHoverPreview() {
        hoverDelay.stop(); dismissDelay.stop();
        if (!hoverPreview || hoverPreview->isHidden()) return;
        const int duration = desktopAnimationDuration(180);
        if (!duration) { hideHoverPreview(); return; }
        hoverFade.stop(); hoverFade.setDuration(duration); hoverFade.setStartValue(hoverOpacity->opacity()); hoverFade.setEndValue(0.); hoverFade.start();
    }
    void hideHoverPreview() {
        hoverDelay.stop(); dismissDelay.stop(); hoverMotion.stop(); hoverFade.stop();
        if (hoverOpacity) hoverOpacity->setOpacity(1.);
        if (hoverPreview) hoverPreview->hide();
        hoverSource.clear();
    }
    void showHoverPreview() {
        if (!hoverSource || !hoverSource->isVisible() || !isVisible()) return;
        const QRect card(hoverSource->mapTo(viewport(), QPoint()), hoverSource->size());
        if (!viewport()->rect().intersects(card)) return;
        hoverFade.stop(); hoverOpacity->setOpacity(1.);
        const auto &app = hoverSource->app;
        hoverPreview->app = app; hoverPreview->group = hoverSource->group;
        QString detail = plainDescription(app.description).section('\n', 0, 0).trimmed();
        if (detail.size() > 200) detail = detail.left(197) + "…";
        QStringList facts{hoverSource->group, sourceSummary(app)}; facts.removeAll(QString());
        hoverPreview->app.summary = app.summary + "\n\n" + facts.join(" · ");
        if (!detail.isEmpty() && detail != app.summary) hoverPreview->app.summary += "\n\n" + detail;
        hoverPreview->artwork = hoverSource->artwork;
        if (hoverPreview->artwork.isNull()) hoverPreview->app.screenshots.clear();  // No image yet: a text card
        hoverPreview->spotlight = !hoverPreview->artwork.isNull();
        hoverPreview->artworkBelow = !hoverSource->spotlight;
        bindTile(hoverPreview);
        hoverPreview->setAccessibleName(app.name + "; open app details");
        const int width = qMin(qMax(320, card.width() + 48), viewport()->width() - 24);
        const int height = qMin(qMax(card.height() + 24, hoverPreview->heightForWidth(width)), viewport()->height() - 24);
        const int x = qBound(12, card.center().x() - width / 2, qMax(12, viewport()->width() - width - 12));
        const int y = qBound(12, card.center().y() - height / 2, qMax(12, viewport()->height() - height - 12));
        const QRect expanded(x, y, width, height);
        hoverPreview->setGeometry(card); hoverPreview->show(); hoverPreview->raise();
        const int duration = desktopAnimationDuration(220);
        if (duration) { hoverMotion.setDuration(duration); hoverMotion.setStartValue(card); hoverMotion.setEndValue(expanded); hoverMotion.start(); }
        else hoverPreview->setGeometry(expanded);
    }
    void updateAutoplay() {
        const bool focused = keyboardCarouselFocus && keyboardCarouselFocus == QApplication::focusWidget();
        const QRect visibleCard(carousel->mapTo(viewport(), QPoint()), carousel->size());
        if (!carousel->isVisible() || carousel->count() < 2 || !viewport()->rect().intersects(visibleCard) || !hovered.isEmpty() || focused) autoplay.stop();
        else autoplay.start();
    }
};
