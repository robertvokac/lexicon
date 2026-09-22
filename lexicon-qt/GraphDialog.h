#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPushButton;

// The items around one item as a graph: links are arrows labelled with their
// type. A click centres the graph on another item; a double click opens it.
// The wheel or the zoom buttons zoom, Fit shows it all, and Full screen (F11)
// gives it the whole screen.
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
    // Scales the view by [factor], within limits.
    void zoomBy(double factor);
    // Shows the whole graph, but never larger than life.
    void fit();
    void setFullScreen(bool fullScreen);
    double zoom() const;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    // Escape leaves full screen before it closes the dialog.
    void keyPressEvent(QKeyEvent* event) override;

private:
    void reload();

    QComboBox* m_depthCombo = nullptr;
    QLabel* m_summary = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QPushButton* m_fullScreenButton = nullptr;
    int m_centreId = -1;
    int m_openedItemId = -1;
    int m_nodeCount = 0;
};
