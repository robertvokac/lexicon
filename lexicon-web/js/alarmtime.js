// An alarm's time: stored as UTC "YYYY-MM-DDTHH:MM:SSZ", edited and shown in
// the browser's local time. Pure, so Node can test it.

const pad = (value) => String(value).padStart(2, '0');

// The value for <input type="datetime-local">: local "YYYY-MM-DDTHH:MM".
export function utcToLocalInput(utc) {
    const date = new Date(utc);
    if (Number.isNaN(date.getTime())) return '';
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`
        + `T${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

// A datetime-local value as UTC "YYYY-MM-DDTHH:MM:SSZ", or '' when empty or invalid.
export function localInputToUtc(value) {
    const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?$/.exec(value || '');
    if (!match) return '';
    const [year, month, day, hours, minutes, seconds] = match.slice(1).map((part) => Number(part || 0));
    const date = new Date(year, month - 1, day, hours, minutes, seconds);
    if (Number.isNaN(date.getTime()) || date.getMonth() !== month - 1) return '';
    return `${date.toISOString().slice(0, 19)}Z`;
}

// The next full hour from now, for a new alarm.
export function nextFullHour(now = new Date()) {
    const date = new Date(now);
    date.setMinutes(0, 0, 0);
    date.setHours(date.getHours() + 1);
    return `${date.toISOString().slice(0, 19)}Z`;
}

// "Wed 2030-01-02 10:15" in local time.
export function formatAlarmTime(utc, locale) {
    const date = new Date(utc);
    if (Number.isNaN(date.getTime())) return utc;
    const weekday = date.toLocaleDateString(locale, { weekday: 'short' });
    return `${weekday} ${utcToLocalInput(utc).replace('T', ' ')}`;
}

export function hasGoneOff(utc, now = new Date()) {
    return new Date(utc).getTime() <= now.getTime();
}
