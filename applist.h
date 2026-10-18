#pragma once
#include <QtWidgets>
#include "prototype.h"

// Native row widgets provide wrapping and icon placement inside the list's selection area.
class AppListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear(); option->icon = QIcon();
    }
};

// One line that ends in "…" instead of growing the row; the full text is the tooltip.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &value, QWidget *parent = nullptr) : QLabel(value, parent) { setToolTip(value); setTextFormat(Qt::PlainText); }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::sizeHint().height()); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setPen(palette().color(foregroundRole()));
        p.drawText(rect(), int(alignment()), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

class AppList : public QListWidget {
public:
    explicit AppList(QWidget *parent = nullptr) : QListWidget(parent) {
        setFrameShape(QFrame::NoFrame); setSpacing(0);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setItemDelegate(new AppListDelegate(this));
        setStyleSheet("QListWidget {background: palette(alternate-base); border: none; border-radius: 10px; padding: 14px;} QListWidget::item {border: none; border-radius: 8px;} QListWidget::item:hover {background: palette(button);} QListWidget::item:selected {background: palette(midlight);}");
    }
    void addApp(const QString &name, const QString &summary, const QIcon &icon, int key, const QString &metadata = {}, PreviewOperations *operations = nullptr, const QString &id = {}, bool installed = false, const QString &badge = {}, bool reinstall = true) {
        auto *item = new QListWidgetItem(name + '\n' + summary, this);
        item->setData(Qt::UserRole, key); item->setData(Qt::AccessibleTextRole, name + ". " + summary + ". " + metadata);
        auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(8, 0, 8, 0); layout->setSpacing(12);
        auto *symbol = new QLabel; symbol->setFixedSize(40, 40); symbol->setPixmap(icon.pixmap(40, 40)); layout->addWidget(symbol, 0, Qt::AlignVCenter);
        auto *copy = new QVBoxLayout; copy->setSpacing(5); copy->addStretch();
        auto addText = [copy](const QString &value, bool bold) {
            QLabel *label;
            if (bold) { label = new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); } else label = new ElidedLabel(value);
            QFont font = label->font(); font.setBold(bold); label->setFont(font); copy->addWidget(label);
        };
        addText(name, true); if (!summary.isEmpty()) addText(summary, false); copy->addStretch();
        layout->addLayout(copy, 1);
        if (badge.isEmpty() && !metadata.isEmpty()) {
            auto *source = new QLabel(metadata); source->setTextFormat(Qt::PlainText); source->setWordWrap(true);
            source->setAlignment(Qt::AlignRight | Qt::AlignVCenter); source->setMaximumWidth(180);
            QFont font = source->font(); font.setPointSize(10); source->setFont(font);
            source->setMinimumWidth(qMin(180, QFontMetrics(font).horizontalAdvance(metadata) + 2));  // Short labels stay on one line
            layout->addWidget(source);
        }
        if (installed) {  // Reinstall, Remove (icon only), then where the app came from. Unavailable actions stay, greyed out.
            for (const QString &action : {QString("Reinstall"), QString("Remove")}) {
                const bool iconOnly = action == "Remove", available = operations && (iconOnly || reinstall);
                auto *button = new QPushButton; button->setObjectName("quick" + action); button->setMinimumHeight(32);
                button->setIcon(QIcon::fromTheme(iconOnly ? "edit-delete" : "view-refresh"));
                button->setProperty("iconOnly", iconOnly);
                if (iconOnly) { button->setFixedWidth(36); button->setAccessibleName("Remove " + name); }
                else button->setText(action);
                if (available) operations->bind(button, id, name, action);
                else {
                    button->setEnabled(false);
                    button->setToolTip(iconOnly ? "This app has no package to remove here." : "Reinstall isn't available for this app.");
                }
                if (iconOnly && available) button->setToolTip("Remove");
                layout->addWidget(button);
            }
            if (!badge.isEmpty()) { auto *mark = new SourceBadge(badge); mark->setToolTip(metadata); layout->addWidget(mark, 0, Qt::AlignVCenter); }
        } else if (operations) {
            auto *button = new QPushButton; button->setObjectName("quickInstall"); button->setMinimumHeight(32);
            button->setIcon(QIcon::fromTheme("system-software-install"));
            operations->bind(button, id, name, "Install"); layout->addWidget(button);
        }
        setItemWidget(item, row);
    }
    void fitRows() {
        int height = 28;
        for (int i = 0; i < count(); ++i) {
            auto *row = itemWidget(item(i));
            // The outer scrollbar can shrink the viewport after Qt caches index-widget geometry.
            row->setFixedWidth(qMax(1, viewport()->width()));
            const int rowHeight = qMax(56, row->layout()->totalHeightForWidth(qMax(1, viewport()->width())));
            item(i)->setSizeHint(QSize(0, rowHeight)); height += rowHeight + spacing() * 2;
        }
        setFixedHeight(qMax(1, height)); setVisible(count() > 0);
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QListWidget::resizeEvent(event); fitRows(); }
};
