#pragma once
#include <QtWidgets>
#include <functional>

class FileDropOverlay : public QWidget {
public:
    QString title, files;
    explicit FileDropOverlay(QWidget *parent) : QWidget(parent) {
        setObjectName("fileDropOverlay"); setAttribute(Qt::WA_TransparentForMouseEvents); hide();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QColor scrim = palette().color(QPalette::Window); scrim.setAlpha(235); p.fillRect(rect(), scrim);
        const QRect box = rect().adjusted(24, 24, -24, -24);
        p.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 12, 12);
        const int center = height() / 2;
        QIcon::fromTheme("document-import").paint(&p, width() / 2 - 28, center - 112, 56, 56);
        QFont heading = font(); heading.setPointSizeF(font().pointSizeF() * 1.6); heading.setBold(true);
        p.setFont(heading); p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRect(40, center - 36, width() - 80, 72), Qt::AlignCenter | Qt::TextWordWrap, title);
        p.setFont(font());
        p.drawText(QRect(48, center + 48, width() - 96, 80), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, files);
    }
};

class FileDropPage : public QScrollArea {
public:
    std::function<void(const QList<QUrl> &)> onFiles;
    explicit FileDropPage(QWidget *parent = nullptr) : QScrollArea(parent) { setAcceptDrops(true); }
    static bool accepts(const QUrl &url) {
        if (!url.isLocalFile()) return false;
        const QFileInfo file(url.toLocalFile());
        const QString suffix = file.suffix().toLower();
        return file.isFile() && file.isReadable() && (suffix == "appimage" || suffix == "flatpak" || suffix == "flatpakref");
    }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        hideOverlay();
        if (!event->mimeData()->hasUrls() || event->mimeData()->urls().isEmpty()) { event->ignore(); return; }
        QStringList types, names;
        for (const auto &url : event->mimeData()->urls()) {
            if (!accepts(url)) { event->ignore(); return; }
            const QFileInfo file(url.toLocalFile());
            const QString type = file.suffix().compare("appimage", Qt::CaseInsensitive) == 0 ? "AppImage" : "Flatpak";
            if (!types.contains(type)) types.append(type);
            names.append(file.fileName());
        }
        auto *host = qobject_cast<QMainWindow *>(window());
        QWidget *surface = host ? host->centralWidget() : viewport();
        if (!overlay) overlay = new FileDropOverlay(surface);
        overlay->title = "Drop to install " + types.join(" / ");
        overlay->files = names.size() <= 3 ? names.join('\n') : QString("%1 files · %2").arg(names.size()).arg(names.first());
        overlay->setAccessibleName(overlay->title + ". " + overlay->files);
        overlay->setGeometry(surface->rect()); overlay->show(); overlay->raise(); overlay->update();
        event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *event) override { event->acceptProposedAction(); }
    void dragLeaveEvent(QDragLeaveEvent *event) override { hideOverlay(); event->accept(); }
    void hideEvent(QHideEvent *event) override { hideOverlay(); QScrollArea::hideEvent(event); }
    void resizeEvent(QResizeEvent *event) override {
        QScrollArea::resizeEvent(event);
        if (overlay) overlay->setGeometry(overlay->parentWidget()->rect());
    }
    void dropEvent(QDropEvent *event) override {
        hideOverlay();
        for (const auto &url : event->mimeData()->urls()) if (!accepts(url)) { event->ignore(); return; }
        if (event->mimeData()->urls().isEmpty()) { event->ignore(); return; }
        if (onFiles) onFiles(event->mimeData()->urls());
        event->acceptProposedAction();
    }
private:
    QPointer<FileDropOverlay> overlay;
    void hideOverlay() { if (overlay) overlay->hide(); }
};

// Preview actions never launch package managers or applications.
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
            binding.button->setText(text);
            binding.button->setEnabled(!busy && text != "Removed");
            binding.button->setAccessibleName(text);
        }
        if (onChanged) onChanged();
    }
};

// Lucide icons (ISC license) drawn on their 24 x 24 grid so every icon shares one stroke and follows the theme color.
inline void drawLucide(QPainter &p, const QString &name, const QRectF &box, const QColor &color) {
    const qreal unit = box.width() / 24;
    auto at = [&](qreal x, qreal y) { return QPointF(box.left() + x * unit, box.top() + y * unit); };
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(at(x1, y1), at(x2, y2)); };
    auto square = [&](qreal x, qreal y, qreal size, qreal radius) { p.drawRoundedRect(QRectF(at(x, y), QSizeF(size * unit, size * unit)), radius * unit, radius * unit); };
    p.save(); p.setRenderHint(QPainter::Antialiasing); p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 2 * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
    } else if (name == "panel-close" || name == "panel-open") {
        square(3, 3, 18, 2); line(9, 3, 9, 21);
        if (name == "panel-close") { line(16, 15, 13, 12); line(13, 12, 16, 9); } else { line(14, 9, 17, 12); line(17, 12, 14, 15); }
    }
    p.restore();
}
inline QIcon lucideIcon(const QString &name, int size = 22) {
    QPixmap pixmap(size * 2, size * 2); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); drawLucide(p, name, QRectF(0, 0, size, size), QApplication::palette().color(QPalette::WindowText));
    return QIcon(pixmap);
}

// A sidebar entry. The icon never moves; widening or narrowing the button only reveals or hides the label.
class NavButton : public QAbstractButton {
public:
    QString glyph;
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
