// Image values in the page: pictures fetched with the session, a thumbnail,
// the full-size viewer and the editor for an Image field.
import { api } from './api.js';
import { errorDialog, openDialog } from './dialogs.js';
import { describeImage, formatImageValue, imageFileName, IMAGE_MEDIA_TYPES, parseImageValue,
    sniffImageType } from './imagevalue.js';
import { button, el } from './utils.js';

// Object URLs by value. A stored file never changes, so one download serves
// every picture of it for the life of the page.
const urls = new Map();

export function imageUrl(value) {
    const image = parseImageValue(value);
    if (!image) return Promise.reject(new Error('This is not an image value.'));
    if (!urls.has(value)) {
        const loading = api.downloadBlob(image.hash)
            // The server sends opaque bytes; the value says what they are.
            .then((blob) => URL.createObjectURL(new Blob([blob], { type: image.mediaType })));
        loading.catch(() => urls.delete(value));
        urls.set(value, loading);
    }
    return urls.get(value);
}

// An <img> that fills in once the picture has arrived.
export function imageElement(value, { alt = '', className = '' } = {}) {
    const img = el('img', { alt, class: className, decoding: 'async' });
    img.classList.add('loading');
    imageUrl(value).then((url) => {
        img.src = url;
        img.classList.remove('loading');
    }).catch(() => {
        img.classList.remove('loading');
        img.classList.add('broken');
        img.alt = `${alt} (cannot be shown)`.trim();
    });
    return img;
}

export async function showImage(value, title) {
    const img = imageElement(value, { alt: title, className: 'image-full' });
    const size = el('span', { class: 'hint' });
    img.addEventListener('load', () => {
        size.textContent = `${describeImage(value)}, ${img.naturalWidth} × ${img.naturalHeight} pixels`;
    });
    const fit = el('input', { type: 'checkbox', checked: true, id: 'image-fit' });
    const frame = el('div', { class: 'image-frame fit' }, [img]);
    fit.addEventListener('change', () => frame.classList.toggle('fit', fit.checked));
    await openDialog({
        title,
        wide: true,
        className: 'image-dialog',
        body: el('div', { class: 'image-viewer' }, [
            frame,
            el('div', { class: 'image-viewer-bar' }, [
                el('label', { class: 'checkbox' }, [fit, ' Fit to window']),
                size,
            ]),
        ]),
        showAccept: false,
        cancelLabel: 'Close',
    });
}

async function download(value, name) {
    const image = parseImageValue(value);
    if (!image) return;
    const url = await imageUrl(value);
    const anchor = el('a', { href: url, download: imageFileName(name, value) });
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
}

// The editor for an Image field: a thumbnail, what the image is, and Choose,
// View, Download and Clear.
export function imageEditor(fieldRecord, initialValue, onChange) {
    let current = initialValue || '';
    const picker = el('input', { type: 'file', accept: IMAGE_MEDIA_TYPES.join(','), hidden: true });
    const thumbnail = el('button', {
        type: 'button', class: 'image-thumbnail', title: 'View the image', 'aria-label': `View ${fieldRecord.name}`,
    });
    const info = el('span', { class: 'hint image-info' });
    const choose = button('Choose image...', { class: 'secondary', onclick: () => picker.click() });
    const view = button('View', { class: 'secondary', onclick: () => showImage(current, fieldRecord.name) });
    const save = button('Download', {
        class: 'secondary',
        onclick: () => download(current, fieldRecord.name).catch((error) => errorDialog(error.message)),
    });
    const clearButton = button('Clear', { class: 'secondary', onclick: () => set('') });

    function render() {
        thumbnail.replaceChildren(current
            ? imageElement(current, { alt: fieldRecord.name })
            : el('span', { class: 'hint', text: 'No image' }));
        thumbnail.disabled = !current;
        info.textContent = describeImage(current);
        for (const control of [view, save, clearButton]) control.disabled = !current;
    }

    function set(value) {
        current = value;
        render();
        onChange(value);
    }

    thumbnail.addEventListener('click', () => { if (current) showImage(current, fieldRecord.name); });
    picker.addEventListener('change', async () => {
        const file = picker.files && picker.files[0];
        picker.value = '';
        if (!file) return;
        // Checked here first so a wrong file is not uploaded at all; the
        // server checks again and has the last word.
        const head = new Uint8Array(await file.slice(0, 16).arrayBuffer());
        if (!sniffImageType(head)) {
            await errorDialog('Choose a PNG, JPEG, GIF, WebP or BMP image.');
            return;
        }
        choose.disabled = true;
        info.textContent = `Uploading ${file.name}...`;
        try {
            const uploaded = await api.uploadBlobDetailed(file);
            if (!uploaded.mediaType) throw new Error('The server does not see an image in this file.');
            set(formatImageValue(uploaded.mediaType, uploaded.hash));
        } catch (error) {
            render();
            await errorDialog(error.message);
        } finally {
            choose.disabled = false;
        }
    });
    render();
    return el('div', { class: 'image-field' }, [
        thumbnail,
        el('div', { class: 'image-side' }, [
            info,
            el('div', { class: 'blob-row' }, [choose, view, save, clearButton]),
        ]),
        picker,
    ]);
}

// The item's Image values as pictures under its content.
export function imageSection(fields, fieldValues) {
    const figures = [];
    for (const fieldRecord of fields) {
        const value = (fieldValues || {})[String(fieldRecord.id)];
        if (fieldRecord.dataType !== 'Image' || !parseImageValue(value)) continue;
        const open = el('button', {
            type: 'button', class: 'image-preview', title: 'View the image',
            onclick: () => showImage(value, fieldRecord.name),
        }, [imageElement(value, { alt: fieldRecord.name })]);
        figures.push(el('figure', { class: 'item-image' }, [
            open, el('figcaption', { text: fieldRecord.name }),
        ]));
    }
    return figures.length ? el('section', { class: 'item-images' }, figures) : null;
}
