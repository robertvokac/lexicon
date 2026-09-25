// The items around one item as a graph: links are arrows labelled with their
// type. A click centres the graph on another item; a double click opens it.
// The graph fits the canvas; it zooms in and out (buttons, + and -, Ctrl and
// the wheel), a drag of the background pans it, and it can fill the window.
// Quiz cards goes through the cards of the items drawn.
import { api } from './api.js';
import { openCardQuiz } from './cards.js';
import { openDialog } from './dialogs.js';
import { layoutGraph } from './graphlayout.js';
import { clear, el, fillSelect, formatItemTitle, linkDescription } from './utils.js';

const SVG = 'http://www.w3.org/2000/svg';
const RADIUS = 26;
const ZOOM_STEP = 1.25;
const MIN_ZOOM = 0.4;
const MAX_ZOOM = 8;

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
    // The drawn graph: its size in layout units, and the zoom over the size
    // that fits the canvas.
    let drawing = null;
    let natural = { width: 1, height: 1 };
    let zoom = 1;
    const status = el('p', { class: 'dialog-error', role: 'alert', hidden: true });
    // Closes the dialog with the item to open.
    const openItem = (id) => {
        opened = id;
        const dialog = canvas.closest('dialog');
        if (dialog) dialog.close();
    };

    // Never drawn larger than life when it fits: a small graph stays small.
    function fittedScale() {
        const room = { width: Math.max(canvas.clientWidth - 4, 1), height: Math.max(canvas.clientHeight - 4, 1) };
        return Math.min(room.width / natural.width, room.height / natural.height, 1);
    }

    // Sizes the drawing for the zoom and keeps the middle of the view where
    // it was.
    function applyZoom() {
        if (!drawing) return;
        const middle = {
            x: (canvas.scrollLeft + canvas.clientWidth / 2) / Math.max(canvas.scrollWidth, 1),
            y: (canvas.scrollTop + canvas.clientHeight / 2) / Math.max(canvas.scrollHeight, 1),
        };
        const scale = fittedScale() * zoom;
        drawing.style.width = `${Math.floor(natural.width * scale)}px`;
        drawing.style.height = `${Math.floor(natural.height * scale)}px`;
        canvas.scrollLeft = middle.x * canvas.scrollWidth - canvas.clientWidth / 2;
        canvas.scrollTop = middle.y * canvas.scrollHeight - canvas.clientHeight / 2;
        zoomOut.disabled = zoom <= MIN_ZOOM;
        zoomIn.disabled = zoom >= MAX_ZOOM;
    }

    function zoomBy(factor) {
        zoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom * factor));
        applyZoom();
    }

    function fit() {
        zoom = 1;
        applyZoom();
        // Fit means the new centre item is in the middle, even after a pan or
        // after selecting a node in an asymmetrical neighbourhood.
        canvas.scrollLeft = (canvas.scrollWidth - canvas.clientWidth) / 2;
        canvas.scrollTop = (canvas.scrollHeight - canvas.clientHeight) / 2;
    }

    const zoomIn = el('button', { type: 'button', class: 'secondary graph-tool', text: '+',
        title: 'Zoom in (+)', 'aria-label': 'Zoom in', onclick: () => zoomBy(ZOOM_STEP) });
    const zoomOut = el('button', { type: 'button', class: 'secondary graph-tool', text: '\u2212',
        title: 'Zoom out (-)', 'aria-label': 'Zoom out', onclick: () => zoomBy(1 / ZOOM_STEP) });
    const fitButton = el('button', { type: 'button', class: 'secondary graph-tool', text: 'Fit',
        title: 'Fit the graph (0)', onclick: fit });
    const fullScreen = el('button', { type: 'button', class: 'secondary graph-tool', text: 'Full screen',
        'aria-pressed': 'false', onclick: () => setFullScreen(!isFullScreen()) });

    const isFullScreen = () => fullScreen.getAttribute('aria-pressed') === 'true';

    // Fills the browser window; the dialog keeps its buttons.
    function setFullScreen(on) {
        const dialog = canvas.closest('dialog');
        if (dialog) dialog.classList.toggle('graph-full-screen', on);
        fullScreen.setAttribute('aria-pressed', String(on));
        fullScreen.textContent = on ? 'Exit full screen' : 'Full screen';
        applyZoom();
    }

    // A size change of the canvas (full screen, a resized window) fits the
    // drawing again at the same zoom.
    if (typeof ResizeObserver !== 'undefined') {
        new ResizeObserver(() => applyZoom()).observe(canvas);
    }

    canvas.addEventListener('wheel', (event) => {
        if (!event.ctrlKey) return; // A plain wheel scrolls.
        event.preventDefault();
        zoomBy(event.deltaY < 0 ? ZOOM_STEP : 1 / ZOOM_STEP);
    }, { passive: false });

    // A mouse drag on the background pans; a finger scrolls the canvas anyway.
    canvas.addEventListener('pointerdown', (event) => {
        if (event.pointerType !== 'mouse' || event.button !== 0 || event.target.closest('.graph-node')) return;
        const start = { x: event.clientX, y: event.clientY, left: canvas.scrollLeft, top: canvas.scrollTop };
        canvas.setPointerCapture(event.pointerId);
        canvas.classList.add('panning');
        const move = (moved) => {
            canvas.scrollLeft = start.left - (moved.clientX - start.x);
            canvas.scrollTop = start.top - (moved.clientY - start.y);
        };
        const stop = () => {
            canvas.classList.remove('panning');
            canvas.removeEventListener('pointermove', move);
            canvas.removeEventListener('pointerup', stop);
            canvas.removeEventListener('pointercancel', stop);
        };
        canvas.addEventListener('pointermove', move);
        canvas.addEventListener('pointerup', stop);
        canvas.addEventListener('pointercancel', stop);
    });

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
        const margin = RADIUS + 60;
        // The layout holds node 0 at the origin. A bounding box around all
        // nodes can have an off-centre midpoint, so use symmetric bounds.
        const halfWidth = Math.max(...points.map((point) => Math.abs(point.x))) + margin;
        const halfHeight = Math.max(...points.map((point) => Math.abs(point.y))) + margin;
        const minX = -halfWidth;
        const minY = -halfHeight;
        const width = 2 * halfWidth;
        const height = 2 * halfHeight;
        natural = { width, height };
        drawing = svg('svg', {
            viewBox: `${minX} ${minY} ${width} ${height}`,
            class: 'graph',
            role: 'img',
            'aria-label': `Relationship graph of ${graph.nodes.length} items`,
        }, [svg('defs', {}, [svg('marker', {
            id: 'graph-arrow', viewBox: '0 0 10 10', refX: 10, refY: 5,
            markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse',
        }, [svg('path', { d: 'M 0 0 L 10 5 L 0 10 z', class: 'graph-arrow' })])])]);
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
        // Another centre is another graph: it starts fitted.
        fit();
        summary.textContent = `${graph.nodes.length} item(s), ${graph.edges.length} link(s)`
            + (graph.truncated ? ', more not shown' : '');
    }

    const body = el('div', { class: 'graph-dialog' }, [
        el('div', { class: 'graph-header' }, [el('label', { text: 'Depth:' }), depthSelect,
            el('span', { class: 'graph-summary-slot' }, [summary]),
            el('span', { class: 'graph-tools' }, [zoomOut, zoomIn, fitButton, fullScreen])]),
        canvas,
        status,
        el('p', { class: 'hint', text: 'Click an item to centre on it, double-click or Enter to open it. '
            + 'Zoom with + and \u2212 or Ctrl and the wheel, drag the background to move around.' }),
    ]);
    body.addEventListener('keydown', (event) => {
        if (event.target.matches('select, input, textarea') || event.altKey || event.metaKey) return;
        if (event.key === '+' || event.key === '=') zoomBy(ZOOM_STEP);
        else if (event.key === '-') zoomBy(1 / ZOOM_STEP);
        else if (event.key === '0') fit();
        else return;
        event.preventDefault();
    });

    depthSelect.addEventListener('change', load);
    load();
    const shown = openDialog({
        title: 'Relationship graph',
        body,
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
        extraActions: [
            { label: 'Open centre', onClick: () => openItem(centre) },
            // The same centre and depth: the quiz covers the items drawn here.
            {
                label: 'Quiz cards',
                onClick: () => openCardQuiz({ itemId: centre, depth: Number.parseInt(depthSelect.value, 10) || 2 }),
            },
        ],
    });
    // Escape leaves full screen first, the next one closes the graph. The
    // key itself, as the browser may not let a page cancel the cancel event.
    const dialog = canvas.closest('dialog');
    if (dialog) {
        dialog.addEventListener('keydown', (event) => {
            if (event.key !== 'Escape' || !isFullScreen()) return;
            event.preventDefault();
            setFullScreen(false);
        }, true);
    }
    return shown.then(() => opened);
}
