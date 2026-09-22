// Image values: "<media type>:<SHA-256 of the file>", such as
// "image/png:6c7d...". Pure, so Node can test it; the C++ counterpart is
// lexicon-core/ImageValue.h.

// The image types Lexicon stores and shows. No SVG: it can carry script.
export const IMAGE_MEDIA_TYPES = ['image/png', 'image/jpeg', 'image/gif', 'image/webp', 'image/bmp'];

const NAMES = {
    'image/png': 'PNG', 'image/jpeg': 'JPEG', 'image/gif': 'GIF', 'image/webp': 'WebP', 'image/bmp': 'BMP',
};
const EXTENSIONS = {
    'image/png': 'png', 'image/jpeg': 'jpg', 'image/gif': 'gif', 'image/webp': 'webp', 'image/bmp': 'bmp',
};

export function parseImageValue(value) {
    const match = /^(image\/[a-z]+):([0-9a-f]{64})$/.exec(value || '');
    if (!match || !IMAGE_MEDIA_TYPES.includes(match[1])) return null;
    return { mediaType: match[1], hash: match[2] };
}

export function formatImageValue(mediaType, hash) {
    return `${mediaType}:${hash}`;
}

// The image type the bytes begin with, or '' for anything else. The first 16
// bytes are enough.
export function sniffImageType(bytes) {
    const starts = (prefix, at = 0) => bytes.length >= at + prefix.length
        && prefix.every((byte, index) => bytes[at + index] === byte);
    const ascii = (text) => [...text].map((ch) => ch.charCodeAt(0));
    if (starts([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])) return 'image/png';
    if (starts([0xff, 0xd8, 0xff])) return 'image/jpeg';
    if (starts(ascii('GIF87a')) || starts(ascii('GIF89a'))) return 'image/gif';
    if (starts(ascii('RIFF')) && starts(ascii('WEBP'), 8)) return 'image/webp';
    if (starts(ascii('BM')) && bytes.length >= 14) return 'image/bmp';
    return '';
}

// "PNG image", or '' when the value is no image value.
export function describeImage(value) {
    const image = parseImageValue(value);
    return image ? `${NAMES[image.mediaType]} image` : '';
}

// "<name>.<extension>" for Download, without characters file systems refuse.
export function imageFileName(name, value) {
    const image = parseImageValue(value);
    const base = (name || '').trim().replace(/[\\/:*?"<>|]/g, '_') || 'image';
    return image ? `${base}.${EXTENSIONS[image.mediaType]}` : base;
}
