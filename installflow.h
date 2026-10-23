#pragma once
#include "review.h"
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryFile>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <functional>

// Unified diff of two texts, empty when equal. Uses diff(1) on two temporary files.
inline QString unifiedDiff(const QString &before, const QString &after) {
    QTemporaryFile a, b;
    if (!a.open() || !b.open()) return {};
    a.write(before.toUtf8()); a.flush(); b.write(after.toUtf8()); b.flush();
    QProcess diff; diff.start("diff", {"-u", "--label", "approved earlier", "--label", "now", a.fileName(), b.fileName()});
    diff.waitForFinished(5000);
    return QString::fromUtf8(diff.readAllStandardOutput());
}

// The step between "Install" on an AUR package and the build. Nothing is built until the user ticks the box.
class AurReviewDialog : public QDialog {
public:
    std::function<void()> onApprove;
    QPlainTextEdit *recipeView = nullptr;
    QLabel *facts = nullptr, *flags = nullptr;
    QCheckBox *understood = nullptr;
    QPushButton *build = nullptr;

    explicit AurReviewDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName("aurReview"); setWindowTitle("Review before building"); resize(820, 640);
        auto *layout = new QVBoxLayout(this);
        auto *note = new QLabel("AUR packages are recipes written by community members. Nobody at Arch or CachyOS checks them. Read the recipe below before building.");
        note->setWordWrap(true);
        facts = new QLabel; facts->setObjectName("aurFacts"); facts->setWordWrap(true); facts->setTextFormat(Qt::PlainText);
        flags = new QLabel; flags->setObjectName("aurFlags"); flags->setWordWrap(true); flags->setTextFormat(Qt::PlainText);
        recipeView = new QPlainTextEdit; recipeView->setReadOnly(true); recipeView->setObjectName("aurRecipe");
        recipeView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); recipeView->setLineWrapMode(QPlainTextEdit::NoWrap);
        auto *limits = new QLabel("The build runs in a clean chroot, which keeps the build away from your files. It is not a security sandbox: "
            "the finished package is installed as root and its install scripts and programs run with full access to your system.");
        limits->setWordWrap(true);
        understood = new QCheckBox("I read the recipe and the files it uses"); understood->setObjectName("aurUnderstood");
        auto *cancel = new QPushButton("Cancel"); build = new QPushButton("Build in clean chroot"); build->setObjectName("aurBuild"); build->setEnabled(false);
        auto *buttons = new QHBoxLayout; buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(build);
        for (QWidget *w : {static_cast<QWidget *>(note), static_cast<QWidget *>(facts), static_cast<QWidget *>(flags), static_cast<QWidget *>(recipeView), static_cast<QWidget *>(limits), static_cast<QWidget *>(understood)}) layout->addWidget(w);
        layout->setStretchFactor(recipeView, 1); layout->addLayout(buttons);
        connect(understood, &QCheckBox::toggled, build, &QWidget::setEnabled);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(build, &QPushButton::clicked, this, [this] { accept(); if (onApprove) onApprove(); });
    }
    // previous: the recipe approved last time ("" for a first install); then only the changes are shown first.
    void showReview(const AurInfo &info, const QString &recipe, const QString &previous) {
        QStringList lines;
        lines << "Package: " + info.name + (info.base != info.name ? " (built from " + info.base + ")" : "");
        lines << "Maintainer: " + (info.maintainer.isEmpty() ? QString("none (orphaned)") : info.maintainer);
        lines << QString("Votes: %1 · popularity %2").arg(info.votes).arg(info.popularity, 0, 'f', 2);
        if (info.firstSubmitted.isValid()) lines << "First submitted: " + info.firstSubmitted.date().toString(Qt::ISODate);
        if (info.lastModified.isValid()) lines << "Last updated: " + info.lastModified.date().toString(Qt::ISODate);
        if (info.outOfDate.isValid()) lines << "Flagged out of date: " + info.outOfDate.date().toString(Qt::ISODate);
        facts->setText(lines.join('\n'));
        const QStringList found = reviewFlags(recipe, info.url);
        flags->setText(found.isEmpty() ? "No lines matched the store's checklist. That does not mean the recipe is safe."
                                       : "Read these twice:\n• " + found.join("\n• "));
        QString shown = recipe;
        if (!previous.isEmpty()) {
            const QString changes = unifiedDiff(previous, recipe);
            shown = changes.isEmpty() ? "No changes since you approved this recipe last time.\n\n" + recipe : "Changes since you approved this recipe last time:\n\n" + changes + "\n\n══════ Full recipe ══════\n" + recipe;
        }
        recipeView->setPlainText(shown);
        understood->setChecked(false); build->setEnabled(false);
    }
};

#include "leftovers.h"
// Asks what to remove besides the package itself. Settings and data stay unless the user ticks them.
class RemoveDialog : public QDialog {
public:
    QList<QCheckBox *> options, leftoverBoxes;
    QPushButton *remove = nullptr;
    QList<Leftover> leftovers;

    RemoveDialog(const QString &name, const QList<QPair<QString, bool>> &choices, const QList<Leftover> &found, const QString &home, QWidget *parent = nullptr)
        : QDialog(parent), leftovers(found) {
        setObjectName("removeDialog"); setWindowTitle("Remove " + name); setMinimumWidth(520);
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel("Remove " + name + " from this computer?"); intro->setWordWrap(true);
        QFont font = intro->font(); font.setBold(true); intro->setFont(font); layout->addWidget(intro);
        for (const auto &choice : choices) {
            auto *box = new QCheckBox(choice.first); box->setChecked(choice.second); options.append(box); layout->addWidget(box);
        }
        if (!found.isEmpty()) {
            auto *title = new QLabel("Files this app left in your home folder"); title->setWordWrap(true); layout->addWidget(title);
            auto *hint = new QLabel("Caches are rebuilt on demand and are selected. Settings and data may hold your documents or accounts, so they stay unless you select them.");
            hint->setWordWrap(true); layout->addWidget(hint);
            for (const auto &item : found) {
                QString path = item.path; if (path.startsWith(home)) path = "~" + path.mid(home.size());
                auto *box = new QCheckBox(item.kind + " · " + path + " · " + formatSize(item.bytes));
                box->setChecked(item.checkedByDefault); leftoverBoxes.append(box); layout->addWidget(box);
            }
        }
        auto *cancel = new QPushButton("Cancel"); remove = new QPushButton("Remove"); remove->setObjectName("confirmRemove");
        auto *buttons = new QHBoxLayout; buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(remove); layout->addLayout(buttons);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(remove, &QPushButton::clicked, this, &QDialog::accept);
    }
    QStringList chosenPaths() const {
        QStringList paths;
        for (int i = 0; i < leftoverBoxes.size(); ++i) if (leftoverBoxes[i]->isChecked()) paths << leftovers[i].path;
        return paths;
    }
};

#include <QProgressBar>
#include <QToolButton>
// Progress of a package operation without a terminal: steps, one status line, and the raw output on request.
// Steps as nodes on one line. The line fills as the job moves from the first step to the last.
class StepTrack : public QWidget {
public:
    QStringList names; int step = 0; double fraction = 0; bool failed = false;
    explicit StepTrack(const QStringList &labels, QWidget *parent = nullptr) : QWidget(parent), names(labels) {
        setFixedHeight(68);
        fill.setDuration(450); fill.setEasingCurve(QEasingCurve::OutCubic);
        connect(&fill, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { shown = v.toDouble(); update(); });
        pulse.setStartValue(0.0); pulse.setEndValue(1.0); pulse.setDuration(1300); pulse.setLoopCount(-1);
        connect(&pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { glow = v.toDouble(); update(); });
        pulse.start();
    }
    // Steps already done count 1 each; the active step adds its own progress, or a little while its progress is unknown.
    double position() const {
        const double last = names.size() - 1;
        return step >= names.size() ? last : qMin(last - 0.08, step + (fraction > 0 ? fraction * 0.9 : 0.35));  // Only a finished job reaches the last node
    }
    void setStep(int index) { step = index; fraction = 0; retarget(); }
    void setFraction(double value) { fraction = value; retarget(); }
    void finish(bool ok) { failed = !ok; if (ok) { step = names.size(); fraction = 0; } pulse.stop(); glow = 0; fill.stop(); shown = position(); update(); }  // No animation: the window is often busy right after the job ends
    bool filled() const { return step >= names.size(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const int n = names.size(); if (n < 2) return;
        const double margin = 56, y = 22, radius = 11, span = (width() - 2 * margin) / (n - 1);
        const QColor accent = palette().highlight().color(), idle = palette().mid().color(), danger("#da4453");
        const QColor track = palette().color(QPalette::Base);
        auto x = [&](int i) { return margin + i * span; };
        p.setPen(Qt::NoPen);
        p.setBrush(track); p.drawRoundedRect(QRectF(x(0), y - 2.5, span * (n - 1), 5), 2.5, 2.5);
        p.setBrush(failed ? danger : accent); p.drawRoundedRect(QRectF(x(0), y - 2.5, span * qBound(0., shown, double(n - 1)), 5), 2.5, 2.5);
        for (int i = 0; i < n; ++i) {
            const bool done = i < step, active = i == step && !filled();
            const QPointF c(x(i), y);
            if (active && !failed) { p.setPen(Qt::NoPen); QColor halo = accent; halo.setAlphaF(0.35 * (1 - glow)); p.setBrush(halo); p.drawEllipse(c, radius + 10 * glow, radius + 10 * glow); }
            p.setBrush(done || filled() ? accent : active ? (failed ? danger : accent) : palette().window().color());
            p.setPen(QPen(done || active || filled() ? Qt::transparent : idle, 2)); p.drawEllipse(c, radius, radius);
            QPen mark(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); p.setPen(mark); p.setBrush(Qt::NoBrush);
            if (done || filled()) { QPainterPath tick; tick.moveTo(c.x() - 4.5, c.y()); tick.lineTo(c.x() - 1, c.y() + 3.5); tick.lineTo(c.x() + 5, c.y() - 3.5); p.drawPath(tick); }
            else if (active && failed) { p.drawLine(c + QPointF(-3.5, -3.5), c + QPointF(3.5, 3.5)); p.drawLine(c + QPointF(-3.5, 3.5), c + QPointF(3.5, -3.5)); }
            else if (active) { p.setPen(Qt::NoPen); p.setBrush(Qt::white); p.drawEllipse(c, 3.5, 3.5); }
            QFont font = this->font(); font.setBold(active); p.setFont(font);
            p.setPen(active || done || filled() ? palette().text().color() : palette().placeholderText().color());
            p.drawText(QRectF(c.x() - span / 2 - 8, y + radius + 8, span + 16, 24), Qt::AlignHCenter | Qt::AlignTop, names[i]);
        }
    }
private:
    QVariantAnimation fill, pulse; double shown = 0, glow = 0;
    void retarget() {
        const double target = position();
        if (qFuzzyCompare(target + 1, shown + 1)) { update(); return; }
        fill.stop(); fill.setStartValue(shown); fill.setEndValue(target);
        if (isVisible()) fill.start(); else { shown = target; update(); }
    }
};

class JobDialog : public QDialog {
public:
    QLabel *phase = nullptr;
    StepTrack *track = nullptr;
    QPlainTextEdit *log = nullptr;
    QPushButton *close = nullptr;
    QToolButton *details = nullptr;
    QStringList stepNames; int step = 0;
    bool done = false;  // While the operation runs, closing the window only hides it: the store still writes its output here

    explicit JobDialog(const QString &title, const QStringList &names = {}, QWidget *parent = nullptr) : QDialog(parent), stepNames(names.isEmpty() ? QStringList{"Prepare", "Install"} : names) {
        setObjectName("jobDialog"); setWindowTitle(title);  // No setMinimumWidth: it would stop the layout from setting the window minimum
        auto *layout = new QVBoxLayout(this); layout->setSpacing(12);
        layout->setSizeConstraint(QLayout::SetMinimumSize);  // The window never gets smaller than its contents, so nothing overlaps
        auto *heading = new QLabel(title); QFont font = heading->font(); font.setPointSize(font.pointSize() + 4); font.setBold(true); heading->setFont(font);
        track = new StepTrack(stepNames); track->setMinimumWidth(520);
        phase = new QLabel("Starting…"); phase->setWordWrap(true); phase->setTextFormat(Qt::PlainText); phase->setAlignment(Qt::AlignHCenter);
        phase->setForegroundRole(QPalette::PlaceholderText);
        details = new QToolButton; details->setText("Show details"); details->setCheckable(true); details->setAutoRaise(true); details->setArrowType(Qt::RightArrow); details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(5000); log->setMinimumHeight(220); log->hide();
        log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); log->setLineWrapMode(QPlainTextEdit::NoWrap);
        close = new QPushButton("Hide"); close->setToolTip("The operation keeps running; progress stays in Activity");
        auto *buttons = new QHBoxLayout; buttons->addWidget(details); buttons->addStretch(); buttons->addWidget(close);
        for (QWidget *w : {static_cast<QWidget *>(heading), static_cast<QWidget *>(track), static_cast<QWidget *>(phase)}) layout->addWidget(w);
        layout->addWidget(log, 1); layout->addLayout(buttons);
        connect(details, &QToolButton::toggled, this, [this](bool open) { log->setVisible(open); details->setText(open ? "Hide details" : "Show details"); details->setArrowType(open ? Qt::DownArrow : Qt::RightArrow); adjustSize(); });
        connect(close, &QPushButton::clicked, this, &QDialog::hide);
        showStep(0);
    }
    void reject() override { if (done) QDialog::reject(); else hide(); }
    void closeEvent(QCloseEvent *event) override { if (done) QDialog::closeEvent(event); else { event->ignore(); hide(); } }
    void showStep(int index) { step = qBound(0, index, int(stepNames.size())); track->setStep(step); }
    void advanceTo(int index) { if (index > step) showStep(index); }
    // Output arrives in chunks; keep colors and progress-bar redraws out of the log.
    void feed(const QString &chunk) {
        static const QRegularExpression ansi("\\x1b\\[[0-9;?]*[A-Za-z]");
        for (QString line : QString(chunk).remove(ansi).split(QRegularExpression("[\\n\\r]"), Qt::SkipEmptyParts)) {
            line = line.trimmed(); if (line.isEmpty()) continue;
            log->appendPlainText(line);
            const auto percent = QRegularExpression("(\\d{1,3})%").match(line);
            if (percent.hasMatch() && percent.captured(1).toInt() <= 100) track->setFraction(percent.captured(1).toInt() / 100.0);
            phase->setText(line.startsWith("==>") ? line.mid(3).trimmed() : line.startsWith("->") ? line.mid(2).trimmed() : line);
        }
    }
    void finish(bool ok, const QString &message) {
        done = true;
        if (ok) step = stepNames.size();
        track->finish(ok);
        phase->setText(message); close->setText("Close"); close->setToolTip({});
        if (!ok) details->setChecked(true);
    }
};
