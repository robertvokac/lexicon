#include "GraphDialog.h"

#include "CardQuizDialog.h"
#include "GraphLayout.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QMessageBox>
#include <QPalette>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <map>

namespace {
constexpr double kRadius = 26;

QString linkLabel(const lexicon::LinkRecord& link) {
    switch (link.linkType) {
        case lexicon::LinkType::IsA: return "Is A";
        case lexicon::LinkType::PartOf: return "Part Of";
        case lexicon::LinkType::Uses: return "Uses";
        case lexicon::LinkType::DependsOn: return "Depends On";
        case lexicon::LinkType::Implements: return "Implements";
        case lexicon::LinkType::Related: return "Related";
        case lexicon::LinkType::Contrasts: return "Contrasts";
        case lexicon::LinkType::AlternativeTo: return "Alternative To";
        case lexicon::LinkType::ParentOf: return "Parent Of";
        case lexicon::LinkType::Custom: return qtbridge::toQt(link.customValue);
        default: return "Link";
    }
}

// A node: a click centres the graph on it, a double click opens it.
class NodeItem : public QGraphicsEllipseItem {
public:
    NodeItem(GraphDialog* dialog, int itemId, const QRectF& rect)
        : QGraphicsEllipseItem(rect), m_dialog(dialog), m_itemId(itemId) {
        setCursor(Qt::PointingHandCursor);
        setAcceptHoverEvents(true);
    }

protected:
    // A graphics item that is neither selectable nor movable ignores the
    // press, and an ignored press brings neither the release nor the double
    // click here: the clicks were lost and the view scrolled instead.
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QGraphicsEllipseItem::mousePressEvent(event);
            return;
        }
        event->accept();
    }
    // Queued: centring rebuilds the scene, and with it this item.
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton || m_itemId == m_dialog->centreItemId()) return;
        // A press that travelled was a drag over the node, not a click on it.
        if ((event->scenePos() - event->buttonDownScenePos(Qt::LeftButton)).manhattanLength() > 8) return;
        QMetaObject::invokeMethod(m_dialog, [dialog = m_dialog, id = m_itemId] { dialog->centreOn(id); },
                                  Qt::QueuedConnection);
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseDoubleClickEvent(event);
        QMetaObject::invokeMethod(m_dialog, [dialog = m_dialog, id = m_itemId] { dialog->openItem(id); },
                                  Qt::QueuedConnection);
    }

private:
    GraphDialog* m_dialog;
    int m_itemId;
};

class ZoomingView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;

protected:
    void wheelEvent(QWheelEvent* event) override {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
        scale(factor, factor);
    }
};
} // namespace

GraphDialog::GraphDialog(int itemId, QWidget* parent) : QDialog(parent), m_centreId(itemId) {
    setWindowTitle("Relationship graph");
    resize(900, 680);
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("Depth:", this));
    m_depthCombo = new QComboBox(this);
    m_depthCombo->addItem("1 link away", 1);
    m_depthCombo->addItem("2 links away", 2);
    m_depthCombo->addItem("3 links away", 3);
    m_depthCombo->setCurrentIndex(1);
    top->addWidget(m_depthCombo);
    top->addStretch();
    m_summary = new QLabel(this);
    m_summary->setObjectName("graphSummary");
    top->addWidget(m_summary);
    top->addSpacing(12);
    const auto tool = [this, top](const QString& text, const QString& name, const QString& tip) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(name);
        button->setToolTip(tip);
        button->setAutoDefault(false);
        top->addWidget(button);
        return button;
    };
    auto* zoomIn = tool("+", "graphZoomIn", "Zoom in (Ctrl++)");
    auto* zoomOut = tool(QString::fromUtf8("\u2212"), "graphZoomOut", "Zoom out (Ctrl+-)");
    auto* fitButton = tool("Fit", "graphFit", "Show the whole graph (Ctrl+0)");
    m_fullScreenButton = tool("Full screen", "graphFullScreen", "Use the whole screen (F11)");
    m_fullScreenButton->setCheckable(true);
    auto* quizButton = tool("Quiz cards", "graphQuizCards", "A quiz over the cards of the items shown");
    connect(quizButton, &QPushButton::clicked, this, &GraphDialog::quizCards);
    connect(zoomIn, &QPushButton::clicked, this, [this] { zoomBy(1.25); });
    connect(zoomOut, &QPushButton::clicked, this, [this] { zoomBy(1 / 1.25); });
    connect(fitButton, &QPushButton::clicked, this, &GraphDialog::fit);
    connect(m_fullScreenButton, &QPushButton::toggled, this, &GraphDialog::setFullScreen);
    for (const auto& keys : {QKeySequence(QKeySequence::ZoomIn), QKeySequence("Ctrl+=")})
        connect(new QShortcut(keys, this), &QShortcut::activated, this, [this] { zoomBy(1.25); });
    connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated, this, [this] { zoomBy(1 / 1.25); });
    connect(new QShortcut(QKeySequence("Ctrl+0"), this), &QShortcut::activated, this, &GraphDialog::fit);
    connect(new QShortcut(QKeySequence("F11"), this), &QShortcut::activated, m_fullScreenButton, &QPushButton::toggle);
    root->addLayout(top);
    m_scene = new QGraphicsScene(this);
    m_view = new ZoomingView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    root->addWidget(m_view, 1);
    auto* hint = new QLabel("Click an item to centre on it, double-click to open it. The wheel zooms; drag to move.", this);
    hint->setEnabled(false);
    root->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(m_depthCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &GraphDialog::reload);
    reload();
}

void GraphDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    fit();
}

void GraphDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    fit();
}

void GraphDialog::zoomBy(double factor) {
    const double next = zoom() * factor;
    if (next < 0.05 || next > 8) return;
    m_view->scale(factor, factor);
}

double GraphDialog::zoom() const { return m_view->transform().m11(); }

void GraphDialog::setFullScreen(bool fullScreen) {
    if (fullScreen == isFullScreen()) return;
    if (fullScreen) showFullScreen(); else showNormal();
    const QSignalBlocker blocker(m_fullScreenButton);
    m_fullScreenButton->setChecked(fullScreen);
    m_fullScreenButton->setText(fullScreen ? "Exit full screen" : "Full screen");
}

void GraphDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        setFullScreen(false);
        return;
    }
    QDialog::keyPressEvent(event);
}

void GraphDialog::fit() {
    if (m_scene->items().isEmpty()) return;
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    if (m_view->transform().m11() > 1.0) {
        m_view->resetTransform();
        m_view->centerOn(m_scene->sceneRect().center());
    }
}

void GraphDialog::centreOn(int itemId) {
    m_centreId = itemId;
    reload();
}

int GraphDialog::depth() const { return m_depthCombo->currentData().toInt(); }

void GraphDialog::quizCards() {
    // The same centre and depth, so the quiz covers the items drawn here.
    CardQuizDialog quiz(m_centreId, depth(), this);
    quiz.exec();
}

void GraphDialog::openItem(int itemId) {
    m_openedItemId = itemId;
    accept();
}

void GraphDialog::reload() {
    auto graph = services().core.links.neighborhood(m_centreId, m_depthCombo->currentData().toInt(), 150);
    m_scene->clear();
    if (!graph) {
        QMessageBox::critical(this, "Relationship graph", qtbridge::toQt(graph.error().message));
        return;
    }
    m_nodeCount = static_cast<int>(graph->nodes.size());
    std::map<int, int> indexOf;
    std::vector<int> depths;
    for (const auto& node : graph->nodes) {
        indexOf[node.item.id] = static_cast<int>(depths.size());
        depths.push_back(node.depth);
    }
    std::vector<std::pair<int, int>> edges;
    for (const auto& link : graph->edges) edges.emplace_back(indexOf.at(link.fromItemId), indexOf.at(link.toItemId));
    const auto points = graphlayout::layout(depths, edges);

    const QPalette palette = this->palette();
    const QColor text = palette.color(QPalette::Text);
    const QColor lines = palette.color(QPalette::Mid);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    for (const auto& link : graph->edges) {
        const auto& from = points[indexOf.at(link.fromItemId)];
        const auto& to = points[indexOf.at(link.toItemId)];
        const double dx = to.x - from.x, dy = to.y - from.y;
        const double length = std::max(std::hypot(dx, dy), 1.0);
        const QPointF start(from.x + dx / length * kRadius, from.y + dy / length * kRadius);
        const QPointF end(to.x - dx / length * kRadius, to.y - dy / length * kRadius);
        m_scene->addLine(QLineF(start, end), QPen(lines, 1.5));
        // The arrow head points at the target.
        const QPointF back(-dx / length * 10, -dy / length * 10);
        const QPointF side(-dy / length * 5, dx / length * 5);
        m_scene->addPolygon(QPolygonF({end, end + back + side, end + back - side}), QPen(lines), QBrush(lines));
        auto* label = m_scene->addSimpleText(linkLabel(link), small);
        // Muted, but readable on the dark theme too.
        label->setBrush(palette.color(QPalette::PlaceholderText));
        const QRectF box = label->boundingRect();
        label->setPos((start + end) / 2 - QPointF(box.width() / 2, box.height() / 2));
    }
    for (std::size_t i = 0; i < graph->nodes.size(); ++i) {
        const auto& node = graph->nodes[i];
        const auto& point = points[i];
        auto* circle = new NodeItem(this, node.item.id,
                                    QRectF(point.x - kRadius, point.y - kRadius, 2 * kRadius, 2 * kRadius));
        const bool centre = node.depth == 0;
        circle->setBrush(centre ? palette.color(QPalette::Highlight) : palette.color(QPalette::Button));
        circle->setPen(QPen(centre ? palette.color(QPalette::Highlight).darker(140) : lines, centre ? 3 : 1.5));
        const QString title = qtbridge::toQt(node.item.title);
        const QString disambiguation = qtbridge::toQt(node.item.disambiguation);
        circle->setToolTip(QString("%1\n%2 · %3").arg(
            disambiguation.isEmpty() ? title : QString("%1 [%2]").arg(title, disambiguation),
            qtbridge::toQt(node.item.groupName), qtbridge::toQt(node.item.itemTypeName)));
        circle->setData(0, node.item.id);
        m_scene->addItem(circle);
        auto* caption = m_scene->addSimpleText(title.size() > 28 ? title.left(27) + "…" : title);
        caption->setBrush(text);
        const QRectF box = caption->boundingRect();
        caption->setPos(point.x - box.width() / 2, point.y + kRadius + 2);
    }
    // The layout keeps the centre item at (0, 0). Fit symmetrically around it:
    // the items' bounding box can otherwise move the centre off screen centre.
    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);
    const double halfWidth = std::max(std::abs(bounds.left()), std::abs(bounds.right()));
    const double halfHeight = std::max(std::abs(bounds.top()), std::abs(bounds.bottom()));
    m_scene->setSceneRect(-halfWidth, -halfHeight, 2 * halfWidth, 2 * halfHeight);
    fit();
    m_summary->setText(QString("%1 item(s), %2 link(s)%3")
                           .arg(graph->nodes.size())
                           .arg(graph->edges.size())
                           .arg(graph->truncated ? ", more not shown" : ""));
}
