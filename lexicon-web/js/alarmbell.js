// Rings alarms while the web client is open: every half minute it asks the
// server which alarms have gone off and are not dismissed, shows them in a
// panel over the page with Dismiss and Snooze, and - when the browser allows
// it - as a system notification. A dismissal in any client stops the ringing
// everywhere, because the server keeps it.
import { api, ApiError } from './api.js';
import { formatAlarmTime } from './alarmtime.js';
import { button, clear, el } from './utils.js';

const POLL_MS = 30_000;
// An alarm left ringing is announced again after this long.
const REMINDER_MS = 5 * 60_000;

export function notificationsSupported() {
    return typeof window !== 'undefined' && 'Notification' in window;
}

export function notificationPermission() {
    return notificationsSupported() ? Notification.permission : 'unsupported';
}

// Asks the browser; only a click may ask, so this runs from a button.
export async function requestNotifications() {
    if (!notificationsSupported()) return 'unsupported';
    try {
        return await Notification.requestPermission();
    } catch {
        return Notification.permission;
    }
}

export class AlarmBell {
    constructor() {
        this.panel = el('section', {
            class: 'alarm-bell', role: 'region', 'aria-label': 'Alarms that have gone off', hidden: true,
        });
        this.list = el('div', { class: 'alarm-bell-list', 'aria-live': 'assertive' });
        this.error = el('p', { class: 'dialog-error', role: 'alert', hidden: true });
        this.panel.append(
            el('div', { class: 'alarm-bell-head' }, [
                el('strong', { class: 'alarm-bell-title', text: 'Alarm' }),
                button('Dismiss all', { class: 'secondary', onclick: () => this.dismissAll() }),
            ]),
            this.list,
            this.error,
        );
        document.body.appendChild(this.panel);
        this.announced = new Map();
        this.alarms = [];
        this.timer = 0;
        this.onVisible = () => { if (!document.hidden) this.poll(); };
    }

    start() {
        this.stop();
        this.timer = window.setInterval(() => this.poll(), POLL_MS);
        document.addEventListener('visibilitychange', this.onVisible);
        return this.poll();
    }

    stop() {
        window.clearInterval(this.timer);
        this.timer = 0;
        document.removeEventListener('visibilitychange', this.onVisible);
        this.alarms = [];
        this.announced.clear();
        this.render();
    }

    destroy() {
        this.stop();
        this.panel.remove();
    }

    async poll() {
        let due;
        try {
            due = (await api.dueAlarms()).alarms;
        } catch (error) {
            // A server that is briefly away is asked again next time; a lost
            // session is handled by the application.
            if (error instanceof ApiError && error.isUnauthorized) this.stop();
            return;
        }
        const now = Date.now();
        const fresh = [];
        const ringing = new Map();
        for (const alarm of due) {
            const key = `${alarm.id}@${alarm.firesAt}`;
            const seen = this.announced.get(key);
            if (seen === undefined || now - seen >= REMINDER_MS) {
                fresh.push(alarm);
                ringing.set(key, now);
            } else {
                ringing.set(key, seen);
            }
        }
        this.announced = ringing;
        this.alarms = due;
        this.render();
        if (fresh.length) this.announce(fresh);
    }

    render() {
        clear(this.list);
        this.panel.hidden = this.alarms.length === 0;
        this.panel.querySelector('.alarm-bell-title').textContent =
            this.alarms.length === 1 ? 'Alarm' : `${this.alarms.length} alarms`;
        for (const alarm of this.alarms) {
            const card = el('article', { class: 'alarm-card', dataset: { id: String(alarm.id) } }, [
                el('strong', { text: alarm.title }),
                el('span', { class: 'hint', text: formatAlarmTime(alarm.firesAt) }),
            ]);
            if (alarm.description) card.appendChild(el('p', { class: 'alarm-card-description', text: alarm.description }));
            card.appendChild(el('div', { class: 'alarm-card-actions' }, [
                button('Dismiss', { onclick: () => this.act(() => api.dismissAlarm(alarm.id)) }),
                button('Snooze 10 min', { class: 'secondary', onclick: () => this.act(() => api.snoozeAlarm(alarm.id, 10)) }),
                button('Snooze 1 hour', { class: 'secondary', onclick: () => this.act(() => api.snoozeAlarm(alarm.id, 60)) }),
            ]));
            this.list.appendChild(card);
        }
    }

    async act(request) {
        this.error.hidden = true;
        try {
            await request();
        } catch (error) {
            if (!(error instanceof ApiError && error.status === 404)) {
                this.error.textContent = error.message;
                this.error.hidden = false;
            }
        }
        await this.poll();
    }

    async dismissAll() {
        await this.act(() => Promise.all(this.alarms.map((alarm) => api.dismissAlarm(alarm.id)
            .catch((error) => { if (!(error instanceof ApiError && error.status === 404)) throw error; }))));
    }

    announce(fresh) {
        beep();
        if (notificationPermission() !== 'granted') return;
        for (const alarm of fresh) {
            try {
                const notification = new Notification(`Alarm: ${alarm.title}`, {
                    body: alarm.description || formatAlarmTime(alarm.firesAt),
                    tag: `lexicon-alarm-${alarm.id}`,
                    requireInteraction: true,
                });
                notification.onclick = () => {
                    window.focus();
                    notification.close();
                };
            } catch {
                // Some browsers only notify from a service worker; the panel
                // on the page still rings.
            }
        }
    }
}

// A short two-tone chime. Browsers allow sound once the page has been used,
// which signing in is; where they do not, the alarm is silent but visible.
function beep() {
    try {
        const Context = window.AudioContext || window.webkitAudioContext;
        if (!Context) return;
        const context = new Context();
        const gain = context.createGain();
        gain.connect(context.destination);
        gain.gain.setValueAtTime(0.0001, context.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.2, context.currentTime + 0.02);
        gain.gain.exponentialRampToValueAtTime(0.0001, context.currentTime + 0.9);
        [880, 660].forEach((frequency, index) => {
            const tone = context.createOscillator();
            tone.frequency.value = frequency;
            tone.connect(gain);
            tone.start(context.currentTime + index * 0.3);
            tone.stop(context.currentTime + index * 0.3 + 0.28);
        });
        window.setTimeout(() => context.close(), 1500);
    } catch {
        // No sound, no harm.
    }
}
