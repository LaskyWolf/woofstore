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

class AppList : public QListWidget {
public:
    explicit AppList(QWidget *parent = nullptr) : QListWidget(parent) {
        setFrameShape(QFrame::NoFrame); setSpacing(0);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setItemDelegate(new AppListDelegate(this));
        setStyleSheet("QListWidget {background: palette(alternate-base); border: none; border-radius: 10px; padding: 14px;} QListWidget::item {border: none; border-radius: 8px;} QListWidget::item:hover {background: palette(button);} QListWidget::item:selected {background: palette(midlight);}");
    }
    void addApp(const QString &name, const QString &summary, const QIcon &icon, int key, const QString &metadata = {}, PreviewOperations *operations = nullptr, const QString &id = {}, bool installed = false) {
        auto *item = new QListWidgetItem(name + '\n' + summary, this);
        item->setData(Qt::UserRole, key); item->setData(Qt::AccessibleTextRole, name + ". " + summary + ". " + metadata);
        auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(8, 0, 8, 0); layout->setSpacing(12);
        auto *symbol = new QLabel; symbol->setFixedSize(40, 40); symbol->setPixmap(icon.pixmap(40, 40)); layout->addWidget(symbol, 0, Qt::AlignVCenter);
        auto *copy = new QVBoxLayout; copy->setSpacing(5); copy->addStretch();
        auto addText = [copy](const QString &value, bool bold) {
            auto *label = new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
            QFont font = label->font(); font.setBold(bold); label->setFont(font); copy->addWidget(label);
        };
        addText(name, true); if (!summary.isEmpty()) addText(summary, false); copy->addStretch();
        layout->addLayout(copy, 1);
        if (!metadata.isEmpty()) {
            auto *source = new QLabel(metadata); source->setTextFormat(Qt::PlainText); source->setWordWrap(true);
            source->setAlignment(Qt::AlignRight | Qt::AlignVCenter); source->setMaximumWidth(180);
            QFont font = source->font(); font.setPointSize(10); source->setFont(font);
            source->setMinimumWidth(qMin(180, QFontMetrics(font).horizontalAdvance(metadata) + 2));  // Short labels stay on one line
            layout->addWidget(source);
        }
        if (operations) {
            for (const QString &action : installed ? QStringList{"Remove", "Reinstall"} : QStringList{"Install"}) {
                auto *button = new QPushButton; button->setObjectName("quick" + action); button->setMinimumHeight(32);
                button->setIcon(QIcon::fromTheme(action == "Remove" ? "edit-delete" : action == "Reinstall" ? "view-refresh" : "system-software-install"));
                operations->bind(button, id, name, action); layout->addWidget(button);
            }
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
