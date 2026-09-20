#include "FilterHeaderView.h"

#include <QPainter>
#include <QResizeEvent>

FilterHeaderView::FilterHeaderView(QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent) {
    connect(this, &QHeaderView::sectionResized, this, [this] { updateFilterPositions(); });
    connect(this, &QHeaderView::sectionMoved, this, [this] { updateFilterPositions(); });
    connect(this, &QHeaderView::geometriesChanged, this, [this] { updateFilterPositions(); });
}

void FilterHeaderView::setFilterWidget(int column, QWidget* widget) {
    removeFilterWidget(column);
    widget->setParent(viewport());
    m_filterWidgets.insert(column, widget);
    updateFilterPositions();
}

void FilterHeaderView::removeFilterWidget(int column) {
    if (auto* widget = m_filterWidgets.take(column)) delete widget;
}

void FilterHeaderView::updateFilterPositions() {
    for (auto it = m_filterWidgets.cbegin(); it != m_filterWidgets.cend(); ++it) {
        QWidget* widget = it.value();
        const int column = it.key();
        if (column >= count() || isSectionHidden(column)) {
            widget->hide();
            continue;
        }
        const int x = sectionViewportPosition(column);
        const int width = sectionSize(column);
        widget->setGeometry(x + 3, 3, qMax(0, width - 6), FilterRowHeight - 6);
        // Hiding an offscreen editor can reset the table's horizontal scroll position.
        widget->show();
    }
}

QSize FilterHeaderView::sizeHint() const {
    QSize size = QHeaderView::sizeHint();
    size.setHeight(size.height() + FilterRowHeight);
    return size;
}

void FilterHeaderView::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
    const QRect filterRect(rect.left(), rect.top(), rect.width(), FilterRowHeight);
    painter->fillRect(filterRect, palette().button());
    painter->setPen(palette().mid().color());
    painter->drawLine(filterRect.topRight(), filterRect.bottomRight());
    const QRect labelRect = rect.adjusted(0, FilterRowHeight, 0, 0);
    QHeaderView::paintSection(painter, labelRect, logicalIndex);
}

void FilterHeaderView::resizeEvent(QResizeEvent* event) {
    QHeaderView::resizeEvent(event);
    updateFilterPositions();
}

void FilterHeaderView::scrollContentsBy(int dx, int dy) {
    QHeaderView::scrollContentsBy(dx, dy);
    updateFilterPositions();
}
