import assert from 'node:assert/strict';
import test from 'node:test';
import { layoutGraph } from '../js/graphlayout.js';

test('the layout keeps items apart, the centre fixed, and is repeatable', () => {
    const depths = [0, 1, 1, 1, 2, 2, 2, 2, 2];
    const edges = [[0, 1], [0, 2], [0, 3], [1, 4], [1, 5], [2, 6], [3, 7], [3, 8]];
    const points = layoutGraph(depths, edges);
    assert.deepEqual(points[0], { x: 0, y: 0 });
    let closest = Infinity;
    for (let i = 0; i < points.length; i += 1) {
        for (let j = i + 1; j < points.length; j += 1) {
            closest = Math.min(closest, Math.hypot(points[i].x - points[j].x, points[i].y - points[j].y));
        }
    }
    assert.ok(closest > 60, `closest ${closest}`);
    assert.deepEqual(layoutGraph(depths, edges), points);
});
