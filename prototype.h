#pragma once
#include <QtWidgets>
#include <functional>

inline QIcon lucideIcon(const QString &name, int size = 22, QColor ink = QColor());

class PreviewOperations : public QObject {
public:
    struct Activity { QString id, name, action; int progress = 0; bool cleared = false, real = false, failed = false; };
    QList<Activity> activities;
    std::function<void()> onChanged;
    explicit PreviewOperations(QObject *parent = nullptr) : QObject(parent) {
        timer.setInterval(80);
        connect(&timer, &QTimer::timeout, this, [this] {
            bool running = false;
            for (auto &activity : activities) {
                if (activity.real) continue;  // Real operations end through finish()
                activity.progress = qMin(100, activity.progress + 4);
                running |= activity.progress < 100;
            }
            refresh(); if (!running) timer.stop();
        });
    }
    QMetaObject::Connection bind(QPushButton *button, const QString &id, const QString &name, const QString &action = "Install") {
        for (int i = bindings.size() - 1; i >= 0; --i) if (bindings[i].button == button) bindings.removeAt(i);
        bindings.append({button, id, action});
        if (!execute) button->setToolTip("No package changes");
        const auto connection = connect(button, &QPushButton::clicked, this, [this, id, name, action] {
            const auto *last = latest(id);
            if (action == "Install" && ((last && last->progress == 100 && last->action != "Remove" && !last->failed) || (!last && installedState && installedState(id)))) {
                if (onOpen) onOpen(id, name);
                return;
            }
            if (execute && execute(id, name, action)) return;  // Real mode: the caller starts the operation and calls begin()
            start(id, name, action);
        });
        refresh(); return connection;
    }
    std::function<void(const QString &, const QString &)> onOpen;  // id, name
    std::function<bool(const QString &, const QString &, const QString &)> execute;  // id, name, action; true when it took over
    std::function<bool(const QString &)> installedState;  // Is this id installed right now?
    void begin(const QString &id, const QString &name, const QString &action) {  // A real operation is running
        activities.append({id, name, action, 0, false, true}); refresh();
    }
    void finish(const QString &id, bool ok) {
        for (int i = activities.size() - 1; i >= 0; --i) if (activities[i].id == id && activities[i].real && activities[i].progress < 100) {
            activities[i].progress = 100; activities[i].failed = !ok; break;
        }
        refresh();
    }
    void refreshButtons() { refresh(); }
    void start(const QString &id, const QString &name, const QString &action) {
        const auto *last = latest(id);
        if (last && last->progress < 100) return;
        activities.append({id, name, action, 0}); timer.start(); refresh();
    }
    const Activity *latest(const QString &id) const {
        for (int i = activities.size() - 1; i >= 0; --i) if (activities[i].id == id) return &activities[i];
        return nullptr;
    }
    int progress() const {
        int sum = 0, count = 0;
        for (const auto &activity : activities) if (activity.progress < 100) { sum += activity.progress; ++count; }
        return count ? sum / count : 100;
    }
    bool running() const { for (const auto &activity : activities) if (activity.progress < 100) return true; return false; }
private:
    struct Binding { QPointer<QPushButton> button; QString id, action; };
    QList<Binding> bindings;
    QTimer timer;
    void refresh() {
        for (int i = bindings.size() - 1; i >= 0; --i) {
            const auto &binding = bindings[i];
            if (!binding.button) { bindings.removeAt(i); continue; }
            const auto *last = latest(binding.id);
            const bool busy = last && last->progress < 100;
            const bool failed = last && last->failed;
            QString text = binding.action;
            if (!last && binding.action == "Install" && installedState && installedState(binding.id)) text = "Open";
            if (busy) text = last->action == "Remove" ? "Removing…" : last->action == "Reinstall" ? "Reinstalling…" : "Installing…";
            else if (last && !failed && binding.action == "Install" && last->action != "Remove") text = "Open";
            else if (last && !failed && binding.action == "Remove" && last->action == "Remove") text = "Removed";
            const bool iconOnly = binding.button->property("iconOnly").toBool();  // The state still shows as tooltip
            binding.button->setText(iconOnly ? QString() : text);
            binding.button->setProperty("state", text);
            if (iconOnly) binding.button->setToolTip(text);
            binding.button->setEnabled(!busy && text != "Removed");
            if (binding.action == "Install" && !busy) binding.button->setIcon(text == "Open" ? lucideIcon("external-link", 20, binding.button->palette().color(QPalette::ButtonText)) : QIcon::fromTheme("system-software-install"));  // Opening is not installing
            if (!iconOnly) binding.button->setAccessibleName(text);
        }
        if (onChanged) onChanged();
    }
};

// The Arch Linux logo (256 x 256, relative m/c/l/z path) as a shape.
inline QPainterPath archLogo() {
    static const QString data = "m127.98 12.07c-10.316 25.309-16.543 41.855-28.031 66.41 7.043 7.4609 15.691 16.156 29.734 25.977-15.098-6.207-25.395-12.445-33.094-18.918-14.703 30.68-37.742 74.391-84.492 158.39 36.746-21.219 65.23-34.293 91.773-39.289-1.1406-4.8945-1.7852-10.195-1.7422-15.734l0.042969-1.1719c0.58203-23.551 12.828-41.645 27.336-40.418 14.508 1.2266 25.781 21.316 25.199 44.867-0.10938 4.4219-0.60938 8.6914-1.4805 12.641 26.258 5.1328 54.438 18.18 90.684 39.105-7.1484-13.156-13.527-25.016-19.621-36.316-9.5938-7.4336-19.605-17.117-40.023-27.594 14.035 3.6406 24.082 7.8516 31.914 12.555-61.941-115.32-66.957-130.66-88.199-180.5z";
    QPainterPath shape; shape.setFillRule(Qt::OddEvenFill);
    static const QRegularExpression token("[mclz]|-?\\d*\\.?\\d+(?:e-?\\d+)?");
    QPointF at; QChar command; QList<qreal> numbers;
    auto flush = [&] {
        if (command == 'm' && numbers.size() >= 2) { at += QPointF(numbers[0], numbers[1]); shape.moveTo(at); for (int i = 2; i + 1 < numbers.size(); i += 2) { at += QPointF(numbers[i], numbers[i + 1]); shape.lineTo(at); } }
        else if (command == 'l') for (int i = 0; i + 1 < numbers.size(); i += 2) { at += QPointF(numbers[i], numbers[i + 1]); shape.lineTo(at); }
        else if (command == 'c') for (int i = 0; i + 5 < numbers.size(); i += 6) { shape.cubicTo(at + QPointF(numbers[i], numbers[i + 1]), at + QPointF(numbers[i + 2], numbers[i + 3]), at + QPointF(numbers[i + 4], numbers[i + 5])); at += QPointF(numbers[i + 4], numbers[i + 5]); }
        else if (command == 'z') shape.closeSubpath();
        numbers.clear();
    };
    for (auto it = token.globalMatch(data); it.hasNext();) {
        const QString text = it.next().captured();
        if (text[0].isLetter()) { flush(); command = text[0]; } else numbers << text.toDouble();
    }
    flush();
    return shape;
}

// Lucide icons (ISC license) drawn on their 24 x 24 grid so every icon shares one stroke and follows the theme color.
inline void drawLucide(QPainter &p, const QString &name, const QRectF &box, const QColor &color, qreal weight = 2) {
    const qreal unit = box.width() / 24;
    auto at = [&](qreal x, qreal y) { return QPointF(box.left() + x * unit, box.top() + y * unit); };
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(at(x1, y1), at(x2, y2)); };
    auto square = [&](qreal x, qreal y, qreal size, qreal radius) { p.drawRoundedRect(QRectF(at(x, y), QSizeF(size * unit, size * unit)), radius * unit, radius * unit); };
    p.save(); p.setRenderHint(QPainter::Antialiasing); p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, weight * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "arch" || name == "aur") {  // Monochrome logos: Arch filled, the AUR (a user-built layer on top of Arch) as an outline
        p.translate(box.topLeft()); p.scale(box.width() / 256, box.width() / 256);
        if (name == "arch") { p.setPen(Qt::NoPen); p.setBrush(color); } else p.setPen(QPen(color, 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(archLogo()); p.restore(); return;
    }
    if (name == "calendar") { line(8, 2, 8, 6); line(16, 2, 16, 6); line(3, 10, 21, 10); square(3, 4, 18, 2); }
    else if (name == "compass") {
        p.drawEllipse(at(12, 12), 10 * unit, 10 * unit);
        const QPointF needle[] = {at(16.24, 7.76), at(14.436, 13.171), at(13.171, 14.436), at(7.76, 16.24), at(9.564, 10.829), at(10.829, 9.564)};
        p.drawPolygon(needle, 6);
    } else if (name == "list") {
        square(3, 3, 7, 1); square(3, 14, 7, 1);
        for (qreal y : {4., 9., 15., 20.}) line(14, y, 21, y);
    } else if (name == "refresh") {
        for (int half = 0; half < 2; ++half) {   // The second half is the first turned around the center
            p.save(); p.translate(at(12, 12)); p.rotate(180 * half); p.translate(-at(12, 12));
            QPainterPath path; path.arcMoveTo(QRectF(at(3, 3), QSizeF(18 * unit, 18 * unit)), 180); path.arcTo(QRectF(at(3, 3), QSizeF(18 * unit, 18 * unit)), 180, -137);
            path.lineTo(at(21, 8)); p.drawPath(path); line(21, 3, 21, 8); line(21, 8, 16, 8); p.restore();
        }
    } else if (name == "package") {
        const QPointF hexagon[] = {at(12, 2.3), at(20.7, 7.2), at(20.7, 16.8), at(12, 21.7), at(3.3, 16.8), at(3.3, 7.2)};
        p.drawPolygon(hexagon, 6); line(3.3, 7.2, 12, 12); line(12, 12, 20.7, 7.2); line(12, 12, 12, 21.7);
    } else if (name == "arch") { line(12, 3, 4.5, 21); line(12, 3, 19.5, 21); line(8, 16.5, 16, 16.5);
    } else if (name == "app-window") { square(2, 4, 20, 2); line(2, 9, 22, 9); line(6, 6.5, 6.1, 6.5); line(9, 6.5, 9.1, 6.5);
    } else if (name == "trophy") {
        QPainterPath path;  // Cup, with the two handles, stem and base
        path.moveTo(at(6, 2)); path.lineTo(at(18, 2)); path.lineTo(at(18, 9)); path.arcTo(QRectF(at(6, 3), QSizeF(12 * unit, 12 * unit)), 0, -180); path.closeSubpath();
        p.drawPath(path);
        QPainterPath left; left.moveTo(at(6, 9)); left.lineTo(at(4.5, 9)); left.arcTo(QRectF(at(2, 4), QSizeF(5 * unit, 5 * unit)), -90, -180); left.lineTo(at(6, 4)); p.drawPath(left);
        QPainterPath right; right.moveTo(at(18, 9)); right.lineTo(at(19.5, 9)); right.arcTo(QRectF(at(17, 4), QSizeF(5 * unit, 5 * unit)), -90, 180); right.lineTo(at(18, 4)); p.drawPath(right);
        line(4, 22, 20, 22); line(10, 14.7, 10, 17); line(14, 14.7, 14, 17); line(10, 17, 7, 22); line(14, 17, 17, 22);
    } else if (name == "external-link") {
        line(15, 3, 21, 3); line(21, 3, 21, 9); line(10, 14, 21, 3);
        QPainterPath box; box.moveTo(at(18, 13)); box.lineTo(at(18, 19)); box.arcTo(QRectF(at(14, 17), QSizeF(4 * unit, 4 * unit)), 0, -90); box.lineTo(at(5, 21)); box.arcTo(QRectF(at(3, 19), QSizeF(4 * unit, 4 * unit)), -90, -90);
        box.lineTo(at(3, 8)); box.arcTo(QRectF(at(3, 6), QSizeF(4 * unit, 4 * unit)), 180, -90); box.lineTo(at(11, 6)); p.drawPath(box);
    } else if (name == "shield") {
        QPainterPath shield; shield.moveTo(at(12, 2.5)); shield.lineTo(at(20, 5.5)); shield.lineTo(at(20, 12));
        shield.cubicTo(at(20, 17), at(16, 20.5), at(12, 22)); shield.cubicTo(at(8, 20.5), at(4, 17), at(4, 12)); shield.lineTo(at(4, 5.5)); shield.closeSubpath();
        p.drawPath(shield); line(9, 12, 11, 14); line(11, 14, 15, 10);
    } else if (name == "gamepad") {
        p.drawRoundedRect(QRectF(at(2.5, 6), QSizeF(19 * unit, 12 * unit)), 6 * unit, 6 * unit); line(7, 10, 7, 14); line(5, 12, 9, 12); line(15, 11, 15.01, 11); line(18, 13, 18.01, 13);
    } else if (name == "monitor") {
        p.drawRoundedRect(QRectF(at(2, 3), QSizeF(20 * unit, 14 * unit)), 2 * unit, 2 * unit); line(8, 21, 16, 21); line(12, 17, 12, 21);
    } else if (name == "cpu") {
        p.drawRoundedRect(QRectF(at(4, 4), QSizeF(16 * unit, 16 * unit)), 2 * unit, 2 * unit); p.drawRoundedRect(QRectF(at(9, 9), QSizeF(6 * unit, 6 * unit)), unit, unit);
        for (const qreal edge : {9., 15.}) { line(edge, 1, edge, 4); line(edge, 20, edge, 23); line(1, edge, 4, edge); line(20, edge, 23, edge); }
    } else if (name == "terminal") {
        line(4, 17, 10, 11); line(10, 11, 4, 5); line(12, 19, 20, 19);
    } else if (name == "grid") { square(3, 3, 7, 1); square(14, 3, 7, 1); square(3, 14, 7, 1); square(14, 14, 7, 1);
    } else if (name == "circle-check") { p.drawEllipse(at(12, 12), 10 * unit, 10 * unit); line(8, 12, 11, 15); line(11, 15, 16, 9);
    } else if (name == "search-x") { p.drawEllipse(at(11, 11), 8 * unit, 8 * unit); line(21, 21, 16.65, 16.65); line(13.5, 8.5, 8.5, 13.5); line(8.5, 8.5, 13.5, 13.5);
    } else if (name == "circle-x") { p.drawEllipse(at(12, 12), 10 * unit, 10 * unit); line(15, 9, 9, 15); line(9, 9, 15, 15);
    } else if (name == "newspaper") {
        p.drawRoundedRect(QRectF(at(6, 2), QSizeF(16 * unit, 20 * unit)), 2 * unit, 2 * unit);
        const QPointF flap[] = {at(6, 9), at(4, 9), at(2, 11), at(2, 20), at(4, 22), at(6, 22)};
        p.drawPolyline(flap, 6); line(18, 14, 10, 14); line(15, 18, 10, 18); p.drawRect(QRectF(at(10, 6), QSizeF(8 * unit, 4 * unit)));
    } else if (name == "trending") { line(22, 7, 13.5, 15.5); line(13.5, 15.5, 8.5, 10.5); line(8.5, 10.5, 2, 17); line(16, 7, 22, 7); line(22, 7, 22, 13);
    } else if (name == "star") {
        QPolygonF star; for (int i = 0; i < 10; ++i) { const qreal angle = M_PI * (-0.5 + i / 5.), radius = i % 2 ? 4.6 : 10; star << at(12 + radius * std::cos(angle), 12.8 + radius * std::sin(angle)); }
        p.drawPolygon(star);
    } else if (name == "sparkle") {
        const QPointF spark[] = {at(12, 2.5), at(14.3, 9.7), at(21.5, 12), at(14.3, 14.3), at(12, 21.5), at(9.7, 14.3), at(2.5, 12), at(9.7, 9.7)};
        p.drawPolygon(spark, 8);
    } else if (name == "panel-close" || name == "panel-open") {
        square(3, 3, 18, 2); line(9, 3, 9, 21);
        if (name == "panel-close") { line(16, 15, 13, 12); line(13, 12, 16, 9); } else { line(14, 9, 17, 12); line(17, 12, 14, 15); }
    }
    p.restore();
}
inline QIcon lucideIcon(const QString &name, int size, QColor ink) {
    QPixmap pixmap(size * 2, size * 2); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); drawLucide(p, name, QRectF(0, 0, size, size), ink.isValid() ? ink : QApplication::palette().color(QPalette::WindowText));
    return QIcon(pixmap);
}

// Calls a function for the events of the objects it watches, without subclassing them.
class EventHook : public QObject {
public:
    std::function<void(QObject *, QEvent *)> on;
    EventHook(QObject *parent, std::function<void(QObject *, QEvent *)> handler) : QObject(parent), on(std::move(handler)) {}
    bool eventFilter(QObject *object, QEvent *event) override { on(object, event); return false; }
};

// A page that scrolls. When it needs no scrollbar, the room for it stays empty, so every page has the same width.
class PageScroll : public QScrollArea {
public:
    explicit PageScroll(QWidget *parent = nullptr) : QScrollArea(parent) {
        auto *bar = verticalScrollBar();
        bar->installEventFilter(new EventHook(this, [this](QObject *, QEvent *event) {
            if (event->type() == QEvent::Show || event->type() == QEvent::Hide) QTimer::singleShot(0, this, [this] { syncRoom(); });
        }));
        QTimer::singleShot(0, this, [this] { syncRoom(); });
    }
private:
    int room = 0;
    void syncRoom() {
        const int want = verticalScrollBar()->isVisibleTo(this) ? 0 : style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
        if (want != room) { room = want; setViewportMargins(0, 0, want, 0); }
    }
};

// A header that stays at the top of a PageScroll while its page scrolls. Put the header's widgets in `column`, then call fit().
// A widget's colour as it is painted: inactive windows use the inactive group, which palette().color() alone does not.
inline QColor shown(const QWidget *widget, QPalette::ColorRole role) { return widget->palette().color(widget->isActiveWindow() ? QPalette::Active : QPalette::Inactive, role); }

// The painted strip of a sticky header: a background and a bottom line once it is stuck, nothing while it sits in the page.
class StickyFill : public QWidget {
public:
    bool stuck = false;
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        if (!stuck) return;
        QPainter p(this); p.fillRect(rect(), shown(this, QPalette::Window)); p.setPen(shown(this, QPalette::Mid)); p.drawLine(0, height() - 1, width(), height() - 1);
    }
};

// A placeholder (`slot`) keeps the header's room in the page; the bar itself floats over the viewport.
class StickyBar : public QWidget {
public:
    QVBoxLayout *column;
    StickyBar(QScrollArea *scroll, QVBoxLayout *body) : QWidget(scroll->viewport()), scroll(scroll) {
        setObjectName("stickyBar");
        column = new QVBoxLayout(this); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(12);
        slot = new QWidget; body->addWidget(slot);
        auto *hook = new EventHook(this, [this](QObject *, QEvent *event) {
            if (event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Show) place();
        });
        slot->installEventFilter(hook); scroll->viewport()->installEventFilter(hook);
        QObject::connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { place(); });
    }
    void fit() { column->activate(); slot->setFixedHeight(sizeHint().height()); place(); }
protected:
    // The line is painted, not styled: a stylesheet's palette() ignores the inactive window colours and made the bar look different from the page.
    void paintEvent(QPaintEvent *) override {
        if (!stuck) return;
        QPainter p(this); p.fillRect(rect(), shown(this, QPalette::Window)); p.setPen(shown(this, QPalette::Mid)); p.drawLine(0, height() - 1, width(), height() - 1);
    }
private:
    QScrollArea *scroll; QWidget *slot; bool stuck = false;
    void place() {
        const QPoint home = slot->mapTo(scroll->viewport(), QPoint(0, 0));
        const bool now = home.y() < 0;
        if (now != stuck) {
            stuck = now;
            column->setContentsMargins(now ? home.x() : 0, now ? 8 : 0, now ? home.x() : 0, now ? 8 : 0);
            update();
        }
        // Stuck, the bar spans the whole viewport so nothing shows through at the sides.
        if (now) setGeometry(0, 0, scroll->viewport()->width(), slot->height() + 16);
        else setGeometry(home.x(), home.y(), slot->width(), slot->height());
        raise();
    }
};

// The mirror of StickyBar: a widget at the end of a page that stays at the bottom of the window while the page is longer than it.
class StickyFooter : public QWidget {
public:
    StickyFooter(QScrollArea *scroll, QVBoxLayout *body, QWidget *content) : QWidget(scroll->viewport()), scroll(scroll) {
        setObjectName("stickyFooter");
        row = new QVBoxLayout(this); row->setContentsMargins(0, 0, 0, 0); row->addWidget(content);
        slot = new QWidget; slot->setFixedHeight(qMax(content->sizeHint().height(), content->minimumHeight())); body->addWidget(slot);
        this->content = content;
        auto *hook = new EventHook(this, [this](QObject *, QEvent *event) {
            if (event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest) place();
        });
        slot->installEventFilter(hook); scroll->viewport()->installEventFilter(hook); content->installEventFilter(hook);
        QObject::connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { place(); });
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!stuck) return;
        QPainter p(this); p.fillRect(rect(), shown(this, QPalette::Window)); p.setPen(shown(this, QPalette::Mid)); p.drawLine(0, 0, width(), 0);
    }
private:
    QScrollArea *scroll; QWidget *slot, *content = nullptr; QVBoxLayout *row; bool stuck = false; int bottomPadding = 24;  // The pages end with a 24 px margin
    void place() {
        // The slot follows the content: a hidden button makes it shorter.
        const int height = qMax(content->sizeHint().height(), content->minimumHeight());
        if (slot->height() != height) slot->setFixedHeight(height);
        const QPoint home = slot->mapTo(scroll->viewport(), QPoint(0, 0));
        const int viewHeight = scroll->viewport()->height();
        // Where the page itself would leave the widget with its bottom padding; when that is below the window, it stays at that distance from the bottom.
        const bool now = home.y() + slot->height() + bottomPadding > viewHeight;
        if (now != stuck) {
            stuck = now;
            row->setContentsMargins(now ? home.x() : 0, now ? 8 : 0, now ? home.x() : 0, now ? bottomPadding : 0);
            update();
        }
        if (now) setGeometry(0, viewHeight - slot->height() - 8 - bottomPadding, scroll->viewport()->width(), slot->height() + 8 + bottomPadding);
        else setGeometry(home.x(), home.y(), slot->width(), slot->height());
        raise();
    }
};

// A turning arc for "loading"; it only runs while it is visible.
class Spinner : public QWidget {
public:
    explicit Spinner(int size = 40, QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(size, size); setObjectName("spinner");
        connect(&timer, &QTimer::timeout, this, [this] { angle = (angle + 30) % 360; update(); });
        timer.setInterval(60);
    }
protected:
    void showEvent(QShowEvent *) override { timer.start(); }
    void hideEvent(QHideEvent *) override { timer.stop(); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const qreal pen = width() / 10.; QColor color = shown(this, QPalette::Highlight);
        p.setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap)); p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(pen, pen, width() - 2 * pen, height() - 2 * pen), -angle * 16, -270 * 16);
    }
private:
    QTimer timer; int angle = 0;
};

// A card that opens something when clicked, with the look of the list panels.
// A cover picture: hidden until it has loaded, 16:9, cropped to fill, rounded on top.
class CoverImage : public QWidget {
public:
    explicit CoverImage(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("newsCover"); QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed); policy.setHeightForWidth(true); setSizePolicy(policy); hide();
    }
    void setPixmap(const QPixmap &image) { pixmap = image; setVisible(!pixmap.isNull()); update(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width * 9 / 16; }
    QSize sizeHint() const override { return {320, 180}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path; path.addRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 12), 10, 10); path.addRect(0, 0, 0, 0);
        p.setClipPath(path);
        const QSize size = pixmap.size().scaled(this->size(), Qt::KeepAspectRatioByExpanding);
        p.drawPixmap(QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), pixmap);
    }
private:
    QPixmap pixmap;
};

class ClickFrame : public QFrame {
public:
    std::function<void()> onClick;
    explicit ClickFrame(QWidget *parent = nullptr) : QFrame(parent) { setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus); setAttribute(Qt::WA_Hover); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
        p.setBrush(shown(this, underMouse() || hasFocus() ? QPalette::Midlight : QPalette::AlternateBase));
        p.drawRoundedRect(rect(), 10, 10);
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(palette().color(QPalette::Highlight)); p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10); }
    }
    void mouseReleaseEvent(QMouseEvent *event) override { if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick) onClick(); }
    void keyPressEvent(QKeyEvent *event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) && onClick) onClick(); else QFrame::keyPressEvent(event);
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

// Colour and glyph that identify where an app comes from.
struct SourceStyle { QColor color; QString glyph; };
inline SourceStyle sourceStyle(const QString &source) {
    if (source == "Flatpak") return {QColor("#3584e4"), "package"};
    if (source == "Arch") return {QColor("#1c9fb4"), "arch"};
    if (source == "AUR" || source == "AUR / local") return {QColor("#e5a50a"), "aur"};
    if (source == "AppImage") return {QColor("#9141ac"), "app-window"};
    if (source == "WoofStore") return {QColor("#26a269"), "sparkle"};  // The store itself, updated from its GitHub releases
    return {QColor("#6c757d"), "grid"};
}
class SourceBadge : public QWidget {
public:
    QString source;
    explicit SourceBadge(const QString &name, int size = 28, QWidget *parent = nullptr) : QWidget(parent), source(name) { setFixedSize(size, size); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const auto style = sourceStyle(source);
        p.setPen(Qt::NoPen); p.setBrush(style.color); p.drawRoundedRect(rect(), width() * .25, width() * .25);
        const qreal inset = width() * .2;
        drawLucide(p, style.glyph, QRectF(inset, inset, width() - 2 * inset, width() - 2 * inset), Qt::white);
    }
};

// A card that opens a file chooser on click and takes dropped files of its own kind.
class DropCard : public QAbstractButton {
public:
    QString source, caption;
    std::function<bool(const QUrl &)> accepts;
    std::function<void(const QList<QUrl> &)> onFiles;
    DropCard(const QString &title, const QString &kind, const QString &hint, QWidget *parent = nullptr) : QAbstractButton(parent), source(kind), caption(hint) {
        setText(title); setAcceptDrops(true); setMinimumHeight(84); setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);
    }
    bool dragging = false;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = sourceStyle(source).color;
        QColor fill = shown(this, underMouse() || hasFocus() ? QPalette::Button : QPalette::AlternateBase);
        if (dragging) { fill = accent; fill.setAlphaF(.18); }
        p.setPen(Qt::NoPen); p.setBrush(fill); p.drawRoundedRect(rect(), 12, 12);
        if (dragging) { p.setBrush(Qt::NoBrush); p.setPen(QPen(accent, 2, Qt::DashLine)); p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12); }
        const int badge = 48; const QRectF box(20, (height() - badge) / 2., badge, badge);
        p.setPen(Qt::NoPen); p.setBrush(accent); p.drawRoundedRect(box, 12, 12);
        drawLucide(p, sourceStyle(source).glyph, box.adjusted(11, 11, -11, -11), Qt::white);
        QFont bold = font(); bold.setBold(true); bold.setPointSizeF(bold.pointSizeF() * 1.1);
        p.setFont(bold); p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRectF(box.right() + 16, 0, width() - box.right() - 28, height() / 2. + 2), Qt::AlignLeft | Qt::AlignBottom, text());
        p.setFont(font()); p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRectF(box.right() + 16, height() / 2. + 6, width() - box.right() - 28, height() / 2. - 6), Qt::AlignLeft | Qt::AlignTop, dragging ? "Release to install" : caption);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        bool ok = event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty();
        if (ok) for (const auto &url : event->mimeData()->urls()) ok &= bool(accepts) && accepts(url);
        if (!ok) { event->ignore(); return; }
        dragging = true; update(); event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *event) override { event->acceptProposedAction(); }
    void dragLeaveEvent(QDragLeaveEvent *event) override { dragging = false; update(); event->accept(); }
    void dropEvent(QDropEvent *event) override {
        dragging = false; update();
        if (onFiles) onFiles(event->mimeData()->urls());
        event->acceptProposedAction();
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

// A sidebar entry. The icon never moves; widening or narrowing the button only reveals or hides the label.
class NavButton : public QAbstractButton {
public:
    QString glyph;
    int badge = 0;  // Count shown on the icon, e.g. pending updates
    QColor badgeColor = QColor("#da4453");  // Red asks for attention; the accent colour only informs
    NavButton(const QString &label, const QString &icon, QWidget *parent = nullptr) : QAbstractButton(parent), glyph(icon) {
        setText(label); setCheckable(true); setFixedHeight(40); setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    // 0 while only the icon fits, 1 when the whole label has room.
    static qreal reveal(int width) { return qBound(0., (width - 84) / 48., 1.); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (isChecked() || underMouse()) { p.setPen(Qt::NoPen); p.setBrush(palette().color(isChecked() ? QPalette::Midlight : QPalette::Button)); p.drawRoundedRect(rect(), 8, 8); }
        drawLucide(p, glyph, QRectF(9, height() / 2. - 11, 22, 22), palette().color(QPalette::WindowText));
        if (badge > 0) {
            const QPointF c(32, height() / 2. - 9); p.setPen(Qt::NoPen); p.setBrush(badgeColor); p.drawEllipse(c, 8, 8);
            QFont small = font(); small.setPointSizeF(7.5); small.setBold(true); p.setFont(small); p.setPen(badgeColor.lightness() > 170 ? Qt::black : Qt::white);
            p.drawText(QRectF(c.x() - 8, c.y() - 8, 16, 16), Qt::AlignCenter, badge > 9 ? "9+" : QString::number(badge)); p.setFont(font());
        }
        const qreal shown = reveal(width());
        if (shown > 0) {
            QColor ink = palette().color(QPalette::WindowText); ink.setAlphaF(shown);
            p.setPen(ink); p.drawText(QRectF(44 - (1 - shown) * 10, 0, width() - 48, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        }
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(palette().color(QPalette::Highlight)); p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8); }
    }
};

class ActivityButton : public QAbstractButton {
public:
    bool compact = false, busy = false, indeterminate = false, attention = false;
    int result = 0;  // Last package operation not yet looked at: 0 none, 1 succeeded, 2 failed
    void setResult(int value) {
        if (value == result) return;
        result = value;
        if (value) { pop.stop(); pop.start(); } else update();
    }
    int progress = 100;
    explicit ActivityButton(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setFixedHeight(48); setMinimumWidth(40); setFocusPolicy(Qt::StrongFocus);
        pop.setDuration(260); pop.setStartValue(0.); pop.setEndValue(1.); pop.setEasingCurve(QEasingCurve::OutBack);
        connect(&pop, &QVariantAnimation::valueChanged, this, [this] { update(); });
        setAccessibleName("Show activity"); setToolTip("Show activity"); setCursor(Qt::PointingHandCursor);
    }
protected:
    // The check or cross springs in when a package operation ends.
    void drawCalendar(QPainter &p, const QRectF &box) {
        const qreal unit = box.width() / 24;
        auto at = [&](qreal px, qreal py) { return QPointF(box.left() + px * unit, box.top() + py * unit); };
        drawLucide(p, "calendar", box, palette().color(QPalette::WindowText));
        if (!result) return;
        p.save(); p.setRenderHint(QPainter::Antialiasing);
        const qreal grow = pop.state() == QAbstractAnimation::Running ? pop.currentValue().toReal() : 1.;
        p.translate(at(12, 16)); p.scale(grow, grow); p.translate(-at(12, 16));
        p.setPen(QPen(QColor(result == 1 ? "#27ae60" : "#da4453"), 2.4 * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (result == 1) { p.drawLine(at(9, 16), at(11, 18)); p.drawLine(at(11, 18), at(15, 14)); }
        else { p.drawLine(at(14, 14), at(10, 18)); p.drawLine(at(10, 14), at(14, 18)); }
        p.restore();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus()) { p.setPen(Qt::NoPen); p.setBrush(palette().color(QPalette::Button)); p.drawRoundedRect(rect(), 8, 8); }
        const int x = 20;  // Same spot whether the sidebar is open or collapsed
        drawCalendar(p, QRectF(x - 11, 9, 22, 22));
        if (busy && compact) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawArc(QRectF(x - 18, 2, 36, 36), 90 * 16, -(indeterminate ? 240 : progress * 360 / 100) * 16);
        }
        if (attention) {  // Yellow dot: something needs the user
            p.setPen(QPen(palette().color(QPalette::Window), 2)); p.setBrush(QColor("#f5c211"));
            p.drawEllipse(QPointF(x + 10, 11), 5, 5);
        }
        if (const qreal shown = NavButton::reveal(width()); shown > 0) {
            QColor ink = palette().color(QPalette::WindowText); ink.setAlphaF(shown);
            p.setPen(ink); p.drawText(QRectF(44 - (1 - shown) * 10, 5, width() - 48, 27), Qt::AlignVCenter, "Activity");
            QColor track = palette().color(QPalette::Mid), bar = palette().color(QPalette::Highlight); track.setAlphaF(shown); bar.setAlphaF(shown);
            p.setPen(Qt::NoPen); p.setBrush(track); p.drawRoundedRect(QRectF(12, 39, width() - 24, 3), 1.5, 1.5);
            p.setBrush(bar); p.drawRoundedRect(QRectF(12, 39, (width() - 24) * (indeterminate ? .3 : progress / 100.), 3), 1.5, 1.5);
        }
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(palette().color(QPalette::Highlight)); p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 8, 8); }
    }
private:
    QVariantAnimation pop;
};
