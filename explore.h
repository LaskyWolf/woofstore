#pragma once
#include "applist.h"
#include <QtWidgets>
#include <algorithm>
#include <functional>

struct DemoApp { QString name, summary, category, id; };

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
    DemoApp app;
    bool hero = false, spotlight = false, artworkBelow = false;
    QPixmap artwork, wideArtwork;
    QPushButton *quickInstall = nullptr;
    explicit ExploreTile(const DemoApp &value, bool featured = false, QWidget *parent = nullptr)
        : QAbstractButton(parent), app(value), hero(featured) {
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
        installBinding = operations->bind(quickInstall, app.id, app.name);
        quickInstall->setAccessibleName("Quick install " + app.name);
        placeQuickInstall();
    }
    int heightForWidth(int width) const override {
        if (hero) return width < 650 ? 220 : 246;
        QFont body = font();
        const int textWidth = !spotlight && !wideArtwork.isNull() && width >= 340 ? int(width * .54) : width;
        return (spotlight ? width / 2 + 110 : 110) + QFontMetrics(body).boundingRect(QRect(0, 0, qMax(80, textWidth - 28), 500), Qt::TextWordWrap, app.summary).height();
    }
    QSize sizeHint() const override { return QSize(190, heightForWidth(190)); }
protected:
    void resizeEvent(QResizeEvent *event) override { QAbstractButton::resizeEvent(event); placeQuickInstall(); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape; shape.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
        p.setClipPath(shape);
        p.fillRect(rect(), palette().color(QPalette::Button));
        QColor wash = categoryColor(app.category); wash.setAlpha(hero ? 48 : 22); p.fillRect(rect(), wash);
        const QIcon icon(":/explore/" + app.id + ".png");
        const QColor ink = palette().color(QPalette::WindowText);
        p.setPen(ink);
        if (hero) {
            const int split = width() * 46 / 100;
            const QPixmap art(":/explore/" + app.id + "-screenshot.png");
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
            const bool wide = !spotlight && !wideArtwork.isNull() && width() >= 340;
            const int textWidth = wide ? int(width() * .54) : width();
            if (wide) {
                const QRect picture(textWidth, 0, width() - textWidth, height());
                const auto scaled = wideArtwork.scaled(picture.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
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
                if (imageHeight > 0) {
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
        // ponytail: bounded bands suit the sample catalogue; virtualize a large live catalogue.
        while (offset < visible.size()) {
            const int span = count >= 4 && band % 2 == 0 ? 2 : 1;
            int size = qMin(int(visible.size()) - offset, 2 * (count - span) + 1);
            if (size > 1 && size % 2 == 0) --size;
            int chosen = -1;
            for (int i = 0; i < size; ++i)
                if (!visible[offset + i]->artwork.isNull()) { chosen = i; break; }
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
    QComboBox *source = nullptr;
    QLineEdit *search = nullptr;
    QToolButton *filters = nullptr;
    QWidget *filterPanel = nullptr;
    ExploreMasonry *masonry = nullptr;
    AppList *alphabet = nullptr;
    PreviewOperations *operations = nullptr;
    QPushButton *loadMore = nullptr;
    void setOperations(PreviewOperations *value) {
        operations = value;
        for (int i = 0; i < carousel->count(); ++i) {
            auto *tile = static_cast<ExploreTile *>(carousel->widget(i)); tile->bindQuickInstall(value);
            if (tile->quickInstall) tile->quickInstall->installEventFilter(this);
        }
        applyFilters();
    }
    FeaturedCarousel *carousel = nullptr;
    std::function<void(const DemoApp &)> onOpen;
    const QList<DemoApp> apps = {
        {"Krita", "Paint a world of your own. Brushes, layers, and room for every idea.", "Graphics & Photography", "org.kde.krita"},
        {"Blender", "From your first shape to an entire 3D world.", "Graphics & Photography", "org.blender.Blender"},
        {"Kdenlive", "Turn your footage into a story worth sharing.", "Audio & Video", "org.kde.kdenlive.desktop"},
        {"Firefox", "Your corner of the web.", "Networking", "org.mozilla.firefox"},
        {"Bottles", "Run Windows apps on your Linux desktop.", "Games", "com.usebottles.bottles"},
        {"GIMP", "Retouch photos, build compositions, and make something unexpected.", "Graphics & Photography", "org.gimp.GIMP"},
        {"Obsidian", "Connect your notes and follow your ideas.", "Productivity", "md.obsidian.Obsidian"},
        {"VLC", "Press play. Almost any format, all in one place.", "Audio & Video", "org.videolan.VLC"},
        {"Inkscape", "Draw crisp vector illustrations, from tiny icons to enormous posters.", "Graphics & Photography", "org.inkscape.Inkscape"},
        {"Builder", "A workspace for your next application.", "Developer Tools", "org.gnome.Builder"},
        {"Solanum", "A little focus, a little break.", "Utilities", "org.gnome.Solanum"},
        {"Spotify", "Find the soundtrack to your day.", "Audio & Video", "com.spotify.Client"},
        {"Boxes", "Try another operating system without leaving your desktop.", "System", "org.gnome.Boxes.desktop"},
        {"Telegram", "Keep your conversations close.", "Networking", "org.telegram.desktop"},
        {"OBS Studio", "Record, stream, and share your screen with the world.", "Audio & Video", "com.obsproject.Studio"},
        {"Pika Backup", "Keep a second copy of the things that matter.", "Utilities", "org.gnome.World.PikaBackup"},
        {"Fractal", "A comfortable home for Matrix conversations.", "Networking", "org.gnome.Fractal"},
        {"Calculator", "For quick sums and bigger questions.", "Utilities", "org.gnome.Calculator"}
    };

    explicit ExplorePage(QWidget *parent = nullptr) : QScrollArea(parent) {
        setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget; setWidget(body);
        auto *layout = new QVBoxLayout(body); layout->setContentsMargins(20, 16, 20, 24); layout->setSpacing(16);
        auto *heading = new QHBoxLayout;
        heading->addWidget(text("Explore", 23, true)); heading->addStretch();
        layout->addLayout(heading);
        auto *searchRow = new QHBoxLayout;
        search = new QLineEdit; search->setObjectName("appSearch"); search->setPlaceholderText("Find your next app");
        search->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition); search->setClearButtonEnabled(true);
        search->setMinimumHeight(38); search->setAccessibleName("Search prototype apps"); searchRow->addWidget(search, 1);
        filters = new QToolButton; filters->setIcon(QIcon::fromTheme("view-filter")); filters->setText("Filters"); filters->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); filters->setCheckable(true);
        filters->setMinimumHeight(38); filters->setAccessibleName("Show or hide filters and categories"); searchRow->addWidget(filters);
        layout->addLayout(searchRow);
        filterPanel = new QWidget; auto *filterLayout = new QVBoxLayout(filterPanel); filterLayout->setContentsMargins(0, 0, 0, 0); filterLayout->setSpacing(12);
        auto *options = new QHBoxLayout;
        source = new QComboBox; source->setObjectName("softwareSource"); source->addItems({"All sources", "Flatpak", "AUR"}); source->setAccessibleName("Prototype software source");
        options->addWidget(text("Source")); options->addWidget(source);
        sort = new QComboBox; sort->addItems({"Curated selection", "Name A–Z", "Name Z–A"}); sort->setAccessibleName("App sort order");
        options->addSpacing(16); options->addWidget(text("Sort")); options->addWidget(sort); options->addStretch();
        auto *reset = new QPushButton("Clear filters"); options->addWidget(reset); filterLayout->addLayout(options);
        auto *categories = new QGridLayout; categories->setSpacing(10);
        int i = 0;
        for (const QString &name : {QString("Audio & Video"), QString("Developer Tools"), QString("Education & Science"), QString("Games"), QString("Graphics & Photography"), QString("Networking"), QString("Productivity"), QString("System"), QString("Utilities")}) {
            const QMap<QString, QString> icons{{"Audio & Video", "applications-multimedia"}, {"Developer Tools", "applications-development"}, {"Education & Science", "applications-science"}, {"Games", "applications-games"}, {"Graphics & Photography", "applications-graphics"}, {"Networking", "applications-internet"}, {"Productivity", "applications-office"}, {"System", "applications-system"}, {"Utilities", "applications-utilities"}};
            QPixmap glyph = QIcon::fromTheme(icons.value(name), QIcon::fromTheme("applications-other")).pixmap(22, 22);
            if (!glyph.isNull()) { QPainter tint(&glyph); tint.setCompositionMode(QPainter::CompositionMode_SourceIn); tint.fillRect(glyph.rect(), Qt::white); }
            QIcon symbol(glyph); symbol.addPixmap(glyph, QIcon::Selected); symbol.addPixmap(glyph, QIcon::Active);
            QString caption = name;
            if (name == "Graphics & Photography" || name == "Education & Science") caption.replace(" & ", " &\n");
            caption.replace('&', "&&");
            auto *item = new QPushButton(symbol, caption); item->setAccessibleName(name); item->setIconSize(QSize(22, 22)); item->setCheckable(true); item->setMinimumHeight(58); item->setCursor(Qt::PointingHandCursor);
            item->setStyleSheet(QString("QPushButton {background:%1;color:white;border:2px solid transparent;border-radius:9px;font-weight:bold;padding:8px;} QPushButton:hover,QPushButton:focus,QPushButton:checked {border-color:white;}").arg(categoryColor(name).name()));
            categoryButtons.append(item); categories->addWidget(item, i / 3, i % 3); ++i;
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
        carousel = new FeaturedCarousel; carousel->setObjectName("featuredCarousel"); carousel->setFixedHeight(246);
        for (int index = 0; index < 3; ++index) {
            auto featuredApp = apps[index];
            featuredApp.summary = QStringList{"Digital painting, your way.", "Build your next 3D world.", "Make every frame count."}[index];
            auto *slide = new ExploreTile(featuredApp, true); slide->installEventFilter(this); carousel->addWidget(slide);
            connect(slide, &QAbstractButton::clicked, this, [this, index] { openPreview(apps[index]); });
        }
        layout->addWidget(carousel);
        carouselDots = new QWidget; auto *dotRow = new QHBoxLayout(carouselDots);
        dotRow->setContentsMargins(0, 0, 0, 0); dotRow->setSpacing(0); dotRow->addStretch();
        auto *dotGroup = new QButtonGroup(this);
        for (int index = 0; index < carousel->count(); ++index) {
            auto *dot = new CarouselDot; dot->setObjectName(QString("featuredDot%1").arg(index)); dot->playful = true; dot->installEventFilter(this); dot->setAccessibleName(QString("Show %1 featured slide, %2 of %3").arg(apps[index].name).arg(index + 1).arg(carousel->count()));
            dot->setToolTip(apps[index].name); dotGroup->addButton(dot); dotRow->addWidget(dot); dots.append(dot);
            connect(dot, &QAbstractButton::clicked, this, [this, index] { advance(index - carousel->currentIndex()); });
        }
        dots.first()->setChecked(true); dotRow->addStretch(); layout->addWidget(carouselDots);
        autoplay.setParent(this); autoplay.setObjectName("featuredAutoplay"); autoplay.setInterval(5000);
        connect(&autoplay, &QTimer::timeout, this, [this] { advance(1); });
        carousel->installEventFilter(this); carouselDots->installEventFilter(this);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateAutoplay(); });
        picksHeading = text("A little of everything", 12, true); layout->addWidget(picksHeading);
        masonry = new ExploreMasonry; masonry->setObjectName("exploreMasonry");
        for (const auto &app : apps) {
            auto *tile = new ExploreTile(app, false, masonry); masonry->tiles.append(tile);
            tile->installEventFilter(this); tile->setToolTip({});
            tile->wideArtwork = QPixmap(":/explore/" + app.id + "-screenshot.png");
            if (QStringList{"com.usebottles.bottles", "org.gimp.GIMP", "md.obsidian.Obsidian", "com.obsproject.Studio"}.contains(app.id))
                tile->artwork = QPixmap(":/explore/" + app.id + "-screenshot.png");
            connect(tile, &QAbstractButton::clicked, this, [this, app] { openPreview(app); });
        }
        layout->addWidget(masonry);
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
        QFile hoverMetadata(":/explore/details.json"); if (hoverMetadata.open(QIODevice::ReadOnly)) hoverData = QJsonDocument::fromJson(hoverMetadata.readAll()).object();
        countLabel = text("All apps · A–Z", 12, true); layout->addWidget(countLabel);
        alphabet = new AppList; alphabet->setObjectName("alphabeticalApps");
        alphabet->setAccessibleName("Alphabetical prototype app list");
        layout->addWidget(alphabet);
        loadMore = new QPushButton("Load more"); loadMore->setObjectName("loadMore"); loadMore->setMinimumHeight(32);
        loadMore->setToolTip("All bundled sample apps are already visible"); layout->addWidget(loadMore, 0, Qt::AlignHCenter);
        connect(loadMore, &QPushButton::clicked, this, [this] { loadMore->setText("All apps loaded"); loadMore->setEnabled(false); });
        connect(alphabet, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { openPreview(apps[item->data(Qt::UserRole).toInt()]); });
        connect(alphabet, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { openPreview(apps[item->data(Qt::UserRole).toInt()]); });
        empty = text("No matching sample apps. Clear the filters or try another name."); empty->hide(); layout->addWidget(empty);
        layout->addStretch();
        connect(search, &QLineEdit::textChanged, this, [this] { applyFilters(); });
        connect(source, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
        connect(sort, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
        connect(reset, &QPushButton::clicked, this, [this] {
            category.clear(); for (auto *item : categoryButtons) item->setChecked(false);
            search->clear(); source->setCurrentIndex(0); sort->setCurrentIndex(0); applyFilters();
        });
        applyFilters();
    }
    void advance(int step) {
        const int count = carousel->count();
        carousel->select(((carousel->currentIndex() + step) % count + count) % count);
        dots[carousel->currentIndex()]->setChecked(true);
        updateAutoplay();
    }
    void openPreview(const DemoApp &app) { if (onOpen) onOpen(app); }
    void showCollection() { verticalScrollBar()->setValue(picksHeading->mapTo(widget(), QPoint()).y() - 16); }
    void showDirectory() { verticalScrollBar()->setValue(countLabel->mapTo(widget(), QPoint()).y() - 16); }
    bool check() {
        const int initial = alphabet->count();
        if (initial != apps.size() || QPixmap(":/explore/org.kde.krita-screenshot.png").isNull()) return false;
        if (!filterPanel->isHidden() || filters->isChecked() || carousel->height() != (viewport()->width() < 650 ? 220 : 246)) return false;
        filters->click(); filterMotion.setCurrentTime(filterMotion.duration());
        const int filterHeight = filterPanel->height(); filters->click();
        if (filterMotion.state() == QAbstractAnimation::Running) {
            filterMotion.setCurrentTime(filterMotion.duration() / 2);
            if (filterPanel->height() <= 0 || filterPanel->height() >= filterHeight) return false;
            filterMotion.setCurrentTime(filterMotion.duration());
        }
        if (!filterPanel->isHidden()) return false;
        filters->click(); filterMotion.setCurrentTime(filterMotion.duration());
        if (filterPanel->isHidden()) return false;
        dots[1]->click(); if (carousel->currentIndex() != 1 || !dots[1]->isChecked() || dots[0]->isChecked()) return false;
        qobject_cast<QAbstractButton *>(carousel->currentWidget())->click();
        search->setText("firefox"); if (alphabet->count() != 1 || alphabet->item(0)->text().section('\n', 0, 0) != "Firefox") return false;
        search->setText("no such sample app"); if (alphabet->count() || empty->isHidden()) return false;
        search->clear(); source->setCurrentText("AUR"); if (alphabet->count() != 0) return false;
        source->setCurrentIndex(0); filters->click(); filterMotion.setCurrentTime(filterMotion.duration()); if (!filterPanel->isHidden()) return false; filters->click();
        if (categoryButtons.size() != 9) return false;
        const QList<int> expectedCategoryCounts{4, 2, 0, 1, 5, 3, 1, 1, 4};
        for (int i = 0; i < categoryButtons.size(); ++i) {
            categoryButtons[i]->click(); if (alphabet->count() != expectedCategoryCounts[i]) return false;
            categoryButtons[i]->click();
        }
        for (int i = 1; i < alphabet->count(); ++i)
            if (QString::compare(alphabet->item(i - 1)->text(), alphabet->item(i)->text(), Qt::CaseInsensitive) > 0) return false;
        return alphabet->count() == initial;
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (hoverPreview && (object == hoverPreview || object == hoverPreview->quickInstall)) {
            if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
                dismissDelay.stop(); hoverFade.stop(); hoverOpacity->setOpacity(1.);
            }
            if ((event->type() == QEvent::Leave && object == hoverPreview) || (event->type() == QEvent::FocusOut && !hoverPreview->underMouse())) dismissDelay.start();
        }
        if (masonry) for (auto *tile : masonry->tiles) if (object == tile) {
            if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
                hideHoverPreview(); hoverSource = tile; hoverDelay.start();
            }
            if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut) { hoverDelay.stop(); dismissDelay.start(); }
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) { hideHoverPreview(); return true; }
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
        QTimer::singleShot(0, masonry, [this] { masonry->arrange(); });
    }
private:
    QVariantAnimation filterMotion, hoverMotion, hoverFade;
    QGraphicsOpacityEffect *hoverOpacity = nullptr;
    QTimer hoverDelay, dismissDelay;
    ExploreTile *hoverPreview = nullptr;
    QPointer<ExploreTile> hoverSource;
    QJsonObject hoverData;
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
        hoverPreview->app = hoverSource->app;
        const auto metadata = hoverData.value(hoverSource->app.id).toObject();
        QString detail = metadata.value("description").toString().section('\n', 0, 0).trimmed();
        if (detail.size() > 200) detail = detail.left(197) + "…";
        hoverPreview->app.summary = hoverSource->app.summary + "\n\n" + hoverSource->app.category + " · Flatpak";
        if (!detail.isEmpty() && detail != hoverSource->app.summary) hoverPreview->app.summary += "\n\n" + detail;
        const auto screenshots = metadata.value("screenshots").toArray();
        hoverPreview->artwork = QPixmap(":/explore/" + (screenshots.isEmpty() ? hoverSource->app.id + "-screenshot.png" : screenshots.first().toString()));
        hoverPreview->spotlight = !hoverPreview->artwork.isNull();
        hoverPreview->artworkBelow = !hoverSource->spotlight;
        hoverPreview->bindQuickInstall(operations);
        if (hoverPreview->quickInstall) hoverPreview->quickInstall->installEventFilter(this);
        hoverPreview->setAccessibleName(hoverSource->app.name + "; open app details");
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
    QTimer autoplay;
    QSet<QObject *> hovered;
    QPointer<QWidget> keyboardCarouselFocus;
    int wheelDistance = 0;
    bool wheelPixels = false;
    void updateAutoplay() {
        const bool focused = keyboardCarouselFocus && keyboardCarouselFocus == QApplication::focusWidget();
        const QRect visibleCard(carousel->mapTo(viewport(), QPoint()), carousel->size());
        if (!carousel->isVisible() || !viewport()->rect().intersects(visibleCard) || !hovered.isEmpty() || focused) autoplay.stop();
        else autoplay.start();
    }
    QString category;
    QComboBox *sort = nullptr;
    QList<QPushButton *> categoryButtons;
    QLabel *picksHeading = nullptr, *countLabel = nullptr, *empty = nullptr;
    QWidget *carouselDots = nullptr;
    QList<CarouselDot *> dots;
    static QLabel *text(const QString &value, int size = 0, bool bold = false) {
        auto *item = new QLabel(value); item->setTextFormat(Qt::PlainText); item->setWordWrap(true);
        QFont font = item->font(); if (size) font.setPointSize(size); font.setBold(bold); item->setFont(font); return item;
    }
    void applyFilters() {
        hideHoverPreview();
        const QString query = search->text().trimmed();
        const QMap<QString, QStringList> categoryIds{{"Audio & Video", {"AudioVideo"}}, {"Developer Tools", {"Development"}}, {"Education & Science", {"Education", "Science"}}, {"Games", {"Game"}}, {"Graphics & Photography", {"Graphics"}}, {"Networking", {"Network"}}, {"Productivity", {"Office"}}, {"System", {"System"}}, {"Utilities", {"Utility"}}};
        QList<int> matches;
        for (int i = 0; i < apps.size(); ++i) {
            const auto &app = apps[i];
            const auto publisherCategories = hoverData.value(app.id).toObject().value("categories").toArray();
            bool categoryMatch = category.isEmpty();
            for (const auto &id : categoryIds.value(category)) categoryMatch |= publisherCategories.contains(id);
            const bool match = source->currentText() != "AUR" && categoryMatch
                && (app.name.contains(query, Qt::CaseInsensitive) || app.summary.contains(query, Qt::CaseInsensitive));
            masonry->tiles[i]->setVisible(match); if (match) matches.append(i);
        }
        std::sort(matches.begin(), matches.end(), [this](int a, int b) { return QString::compare(apps[a].name, apps[b].name, Qt::CaseInsensitive) < 0; });
        alphabet->clear();
        for (int index : matches)
            alphabet->addApp(apps[index].name, apps[index].summary, QIcon(":/explore/" + apps[index].id + ".png"), index, {}, operations, apps[index].id);
        alphabet->fitRows();
        if (sort->currentIndex() == 0) std::sort(matches.begin(), matches.end());
        if (sort->currentIndex() == 2) std::reverse(matches.begin(), matches.end());
        // Sort discovery independently; the final directory stays alphabetical.
        QList<ExploreTile *> ordered;
        for (int index : matches) ordered.append(masonry->tiles[index]);
        // Filter visibility by the original data, then only arrange the chosen ordering.
        masonry->order = ordered;
        masonry->arrange();
        const bool browse = query.isEmpty() && category.isEmpty() && source->currentText() != "AUR";
        carousel->setVisible(browse); carouselDots->setVisible(browse);
        picksHeading->setText(query.isEmpty() ? "A little of everything" : QString("%1 matching apps").arg(matches.size()));
        picksHeading->setVisible(!matches.isEmpty()); countLabel->setText("All apps · A–Z"); countLabel->setVisible(!matches.isEmpty());
        empty->setText(source->currentText() == "AUR" ? "AUR is not connected in this prototype. Choose All sources or Flatpak to explore the sample apps." : "No matching sample apps. Clear the filters or try another name.");
        empty->setVisible(matches.isEmpty());
        loadMore->setVisible(!matches.isEmpty()); loadMore->setEnabled(true); loadMore->setText("Load more");
    }
};
