#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;

// The items around one item as a graph: links are arrows labelled with their
// type. A click centres the graph on another item; a double click opens it.
class GraphDialog : public QDialog {
    Q_OBJECT

public:
    explicit GraphDialog(int itemId, QWidget* parent = nullptr);
    // The item chosen with a double click, or -1.
    int openedItemId() const { return m_openedItemId; }
    int centreItemId() const { return m_centreId; }
    int nodeCount() const { return m_nodeCount; }

    void centreOn(int itemId);
    void openItem(int itemId);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void reload();
    // Shows the whole graph, but never larger than life.
    void fit();

    QComboBox* m_depthCombo = nullptr;
    QLabel* m_summary = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    int m_centreId = -1;
    int m_openedItemId = -1;
    int m_nodeCount = 0;
};
