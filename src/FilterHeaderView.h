#pragma once

#include <QHeaderView>
#include <QMap>

class FilterHeaderView : public QHeaderView {
public:
    explicit FilterHeaderView(QWidget* parent = nullptr);

    void setFilterWidget(int column, QWidget* widget);
    void removeFilterWidget(int column);
    void updateFilterPositions();

protected:
    QSize sizeHint() const override;
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    static constexpr int FilterRowHeight = 36;
    QMap<int, QWidget*> m_filterWidgets;
};
