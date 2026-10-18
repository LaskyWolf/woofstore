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

class ActivityButton : public QAbstractButton {
public:
    bool compact = false, busy = false, indeterminate = false;
    int progress = 100;
    explicit ActivityButton(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setFixedHeight(48); setMinimumWidth(40); setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("Show activity"); setToolTip("Show activity"); setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus()) { p.setPen(Qt::NoPen); p.setBrush(palette().color(QPalette::Button)); p.drawRoundedRect(rect(), 8, 8); }
        const int x = compact ? width() / 2 : 20;
        QIcon::fromTheme("view-task").paint(&p, x - 10, 10, 20, 20);
        if (busy && compact) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawArc(QRectF(x - 14, 6, 28, 28), 90 * 16, -(indeterminate ? 240 : progress * 360 / 100) * 16);
        }
        if (!compact) {
            p.setPen(palette().color(QPalette::WindowText)); p.drawText(QRect(40, 5, width() - 44, 27), Qt::AlignVCenter, "Activity");
            p.setPen(Qt::NoPen); p.setBrush(palette().color(QPalette::Mid)); p.drawRoundedRect(QRectF(12, 39, width() - 24, 3), 1.5, 1.5);
            p.setBrush(palette().color(QPalette::Highlight));
            p.drawRoundedRect(QRectF(12, 39, (width() - 24) * (indeterminate ? .3 : progress / 100.), 3), 1.5, 1.5);
        }
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(palette().color(QPalette::Highlight)); p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 8, 8); }
    }
};
