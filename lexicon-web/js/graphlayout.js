// Places the nodes of a relationship graph: rings by distance from the centre
// to start with, then a Fruchterman-Reingold pass that pushes every pair apart
// and pulls linked items together. Deterministic, and the same steps as the
// desktop (lexicon-qt/GraphLayout.h) and Android clients. No DOM, so it also
// runs under Node for its tests.

// depths[i] is node i's distance from the centre, node 0; edges are [a, b]
// index pairs. Returns [{ x, y }] with the centre at the origin.
export function layoutGraph(depths, edges) {
    const count = depths.length;
    const RING = 150;
    const IDEAL = 120;
    const ITERATIONS = 250;
    const perDepth = [0, 0, 0, 0];
    const seen = [0, 0, 0, 0];
    for (const depth of depths) perDepth[Math.min(3, Math.max(0, depth))] += 1;
    const points = depths.map(() => ({ x: 0, y: 0 }));
    for (let i = 1; i < count; i += 1) {
        const depth = Math.min(3, Math.max(1, depths[i]));
        const angle = (2 * Math.PI * seen[depth]) / Math.max(1, perDepth[depth]) + depth * 0.7;
        seen[depth] += 1;
        points[i] = { x: RING * depth * Math.cos(angle), y: RING * depth * Math.sin(angle) };
    }
    for (let step = 0; step < ITERATIONS; step += 1) {
        const temperature = 80 * (1 - step / ITERATIONS) + 2;
        const shift = points.map(() => ({ x: 0, y: 0 }));
        for (let i = 0; i < count; i += 1) {
            for (let j = i + 1; j < count; j += 1) {
                const dx = points[i].x - points[j].x;
                const dy = points[i].y - points[j].y;
                const distance = Math.max(Math.hypot(dx, dy), 0.01);
                const force = (IDEAL * IDEAL) / distance;
                shift[i].x += (dx / distance) * force;
                shift[i].y += (dy / distance) * force;
                shift[j].x -= (dx / distance) * force;
                shift[j].y -= (dy / distance) * force;
            }
        }
        for (const [a, b] of edges) {
            if (a === b) continue;
            const dx = points[a].x - points[b].x;
            const dy = points[a].y - points[b].y;
            const distance = Math.max(Math.hypot(dx, dy), 0.01);
            const force = (distance * distance) / IDEAL;
            shift[a].x -= (dx / distance) * force;
            shift[a].y -= (dy / distance) * force;
            shift[b].x += (dx / distance) * force;
            shift[b].y += (dy / distance) * force;
        }
        for (let i = 1; i < count; i += 1) { // The centre stays where it is.
            const length = Math.hypot(shift[i].x, shift[i].y);
            if (length < 1e-9) continue;
            const move = Math.min(length, temperature);
            points[i].x += (shift[i].x / length) * move;
            points[i].y += (shift[i].y / length) * move;
        }
    }
    return points;
}
