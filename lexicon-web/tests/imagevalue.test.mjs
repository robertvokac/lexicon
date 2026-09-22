import assert from 'node:assert/strict';
import { test } from 'node:test';

import { describeImage, formatImageValue, imageFileName, parseImageValue, sniffImageType }
    from '../js/imagevalue.js';

const hash = 'a'.repeat(64);
const bytes = (...parts) => new Uint8Array(parts.flatMap((part) => (typeof part === 'string'
    ? [...part].map((ch) => ch.charCodeAt(0)) : part)));

test('an image is known by its first bytes', () => {
    assert.equal(sniffImageType(bytes([0x89], 'PNG\r\n\x1a\n', [0, 0, 0, 13])), 'image/png');
    assert.equal(sniffImageType(bytes([0xff, 0xd8, 0xff, 0xe0])), 'image/jpeg');
    assert.equal(sniffImageType(bytes('GIF89a', [1, 0])), 'image/gif');
    assert.equal(sniffImageType(bytes('RIFF', [0x24, 0, 0, 0], 'WEBPVP8 ')), 'image/webp');
    assert.equal(sniffImageType(bytes('RIFF', [0x24, 0, 0, 0], 'WAVEfmt ')), '');
    assert.equal(sniffImageType(bytes('BM', [0x46, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0])), 'image/bmp');
    assert.equal(sniffImageType(bytes('<svg xmlns="http://www.w3.org/2000/svg">')), '');
    assert.equal(sniffImageType(new Uint8Array()), '');
});

test('an image value names its type and its file', () => {
    assert.deepEqual(parseImageValue(`image/png:${hash}`), { mediaType: 'image/png', hash });
    assert.equal(formatImageValue('image/gif', hash), `image/gif:${hash}`);
    assert.equal(parseImageValue(hash), null);
    assert.equal(parseImageValue(`image/svg+xml:${hash}`), null);
    assert.equal(parseImageValue(`image/tiff:${hash}`), null);
    assert.equal(parseImageValue(`image/png:${'A'.repeat(64)}`), null);
    assert.equal(parseImageValue(undefined), null);
});

test('an image is described and named for download', () => {
    assert.equal(describeImage(`image/jpeg:${hash}`), 'JPEG image');
    assert.equal(describeImage('text'), '');
    assert.equal(imageFileName('Diagram: v2', `image/jpeg:${hash}`), 'Diagram_ v2.jpg');
    assert.equal(imageFileName('', `image/webp:${hash}`), 'image.webp');
});
