// Small, client-side CSV writer used for the rows already visible in the item
// table. UTF-8 BOM helps spreadsheet applications recognise non-ASCII text.

function quote(value) {
    return `"${String(value ?? '').replaceAll('"', '""')}"`;
}

export function csvText(headers, rows) {
    const lines = [headers, ...rows]
        .map((row) => row.map(quote).join(','));
    return `\ufeff${lines.join('\r\n')}\r\n`;
}

export function downloadCsv(text, name) {
    const blob = new Blob([text], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = name;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
}
