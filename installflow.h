#pragma once
#include "review.h"
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryFile>
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
