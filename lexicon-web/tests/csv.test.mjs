import assert from 'node:assert/strict';
import { test } from 'node:test';

import { csvText } from '../js/csv.js';

test('CSV is UTF-8 spreadsheet friendly and quotes every cell', () => {
    assert.equal(
        csvText(['Title', 'Tags'], [['Příliš "žluťoučký"', 'one,two'], ['Line\nbreak', '']]),
        '\ufeff"Title","Tags"\r\n'
            + '"Příliš ""žluťoučký""","one,two"\r\n'
            + '"Line\nbreak",""\r\n',
    );
});
