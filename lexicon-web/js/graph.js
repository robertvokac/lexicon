// The items around one item as a graph: links are arrows labelled with their
// type. A click centres the graph on another item; a double click opens it.
import { api } from './api.js';
import { openDialog } from './dialogs.js';
import { layoutGraph } from './graphlayout.js';
import { clear, el, fillSelect, formatItemTitle, linkDescription } from './utils.js';

const SVG = 'http://www.w3.org/2000/svg';
const RADIUS = 26;

function svg(tag, attributes = {}, children = []) {
    const node = document.createElementNS(SVG, tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
    for (const child of children) node.appendChild(child);
    return node;
}

function shorten(text) {
    return text.length > 28 ? `${text.slice(0, 27)}…` : text;
}

// Resolves with the ID of the item to open, or null.
export function openGraph(itemId) {
    let centre = itemId;
    let opened = null;
    const depthSelect = el('select', { 'aria-label': 'Depth' });
    fillSelect(depthSelect, [
        { value: 1, label: '1 link away' },
        { value: 2, label: '2 links away' },
        { value: 3, label: '3 links away' },
    ], 2);
    const summary = el('span', { class: 'hint graph-summary' });
    const canvas = el('div', { class: 'graph-canvas' });
    const status = el('p', { class: 'dialog-error', role: 'alert', hidden: true });
    // Closes the dialog with the item to open.
    const openItem = (id) => {
        opened = id;
        const dialog = canvas.closest('dialog');
        if (dialog) dialog.close();
    };

    async function load() {
        status.hidden = true;
        let graph;
        try {
            graph = await api.itemGraph(centre, Number.parseInt(depthSelect.value, 10) || 2);
        } catch (error) {
            if (!error.isUnauthorized) {
                status.textContent = error.message;
                status.hidden = false;
            }
            return;
        }
        draw(graph);
    }

    function draw(graph) {
        clear(canvas);
        const index = new Map(graph.nodes.map((node, position) => [node.id, position]));
        const points = layoutGraph(graph.nodes.map((node) => node.depth),
            graph.edges.map((edge) => [index.get(edge.fromItemId), index.get(edge.toItemId)]));
        const xs = points.map((point) => point.x);
        const ys = points.map((point) => point.y);
        const margin = RADIUS + 60;
        const minX = Math.min(...xs) - margin;
        const minY = Math.min(...ys) - margin;
        const width = Math.max(...xs) - Math.min(...xs) + 2 * margin;
        const height = Math.max(...ys) - Math.min(...ys) + 2 * margin;
        const drawing = svg('svg', {
            viewBox: `${minX} ${minY} ${width} ${height}`,
            class: 'graph',
            role: 'img',
            'aria-label': `Relationship graph of ${graph.nodes.length} items`,
        }, [svg('defs', {}, [svg('marker', {
            id: 'graph-arrow', viewBox: '0 0 10 10', refX: 10, refY: 5,
            markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse',
        }, [svg('path', { d: 'M 0 0 L 10 5 L 0 10 z', class: 'graph-arrow' })])])]);
        // Never drawn larger than life on a wide screen.
        drawing.style.maxWidth = `${Math.max(width, 320)}px`;
        for (const edge of graph.edges) {
            const from = points[index.get(edge.fromItemId)];
            const to = points[index.get(edge.toItemId)];
            const dx = to.x - from.x;
            const dy = to.y - from.y;
            const length = Math.max(Math.hypot(dx, dy), 1);
            const start = { x: from.x + (dx / length) * RADIUS, y: from.y + (dy / length) * RADIUS };
            const end = { x: to.x - (dx / length) * RADIUS, y: to.y - (dy / length) * RADIUS };
            drawing.appendChild(svg('line', {
                x1: start.x, y1: start.y, x2: end.x, y2: end.y, class: 'graph-edge', 'marker-end': 'url(#graph-arrow)',
            }));
            const label = svg('text', {
                x: (start.x + end.x) / 2, y: (start.y + end.y) / 2, class: 'graph-edge-label', 'text-anchor': 'middle',
            });
            label.textContent = linkDescription(edge);
            drawing.appendChild(label);
        }
        graph.nodes.forEach((node, position) => {
            const point = points[position];
            const title = formatItemTitle(node.title, node.disambiguation);
            const group = svg('g', {
                class: node.depth === 0 ? 'graph-node centre' : 'graph-node',
                tabindex: 0,
                role: 'button',
                'aria-label': node.depth === 0 ? `${title}, the centre` : `${title}: centre on it; double click to open`,
            }, [svg('circle', { cx: point.x, cy: point.y, r: RADIUS })]);
            const tooltip = svg('title');
            tooltip.textContent = [title, node.groupName, node.itemTypeName].filter(Boolean).join(' · ');
            group.appendChild(tooltip);
            const caption = svg('text', { x: point.x, y: point.y + RADIUS + 16, 'text-anchor': 'middle', class: 'graph-caption' });
            caption.textContent = shorten(node.title);
            group.appendChild(caption);
            const centreHere = () => {
                if (node.id === centre) return;
                centre = node.id;
                load();
            };
            group.addEventListener('click', centreHere);
            group.addEventListener('dblclick', () => openItem(node.id));
            group.addEventListener('keydown', (event) => {
                if (event.key === 'Enter') {
                    openItem(node.id);
                } else if (event.key === ' ') {
                    event.preventDefault();
                    centreHere();
                }
            });
            drawing.appendChild(group);
        });
        canvas.appendChild(drawing);
        summary.textContent = `${graph.nodes.length} item(s), ${graph.edges.length} link(s)`
            + (graph.truncated ? ', more not shown' : '');
    }

    depthSelect.addEventListener('change', load);
    load();
    return openDialog({
        title: 'Relationship graph',
        body: el('div', { class: 'graph-dialog' }, [
            el('div', { class: 'graph-header' }, [el('label', { text: 'Depth:' }), depthSelect,
                el('span', { class: 'spacer' }), summary]),
            canvas,
            status,
            el('p', { class: 'hint', text: 'Click an item to centre on it, double-click or Enter to open it.' }),
        ]),
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
        extraActions: [{ label: 'Open centre', onClick: () => openItem(centre) }],
    }).then(() => opened);
}
