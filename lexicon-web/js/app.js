// Application shell: configuration, login, the menu bar, themes and the switch
// between the login screen and the main view.
import { api, API_VERSION, ApiError } from './api.js';
import { errorDialog, messageDialog } from './dialogs.js';
import { clearAllDrafts, setDraftOwner } from './drafts.js';
import { exportDictionary, importDictionary } from './exchange.js';
import { openGroupManager } from './groups.js';
import { MainView } from './items.js';
import { showValueOverview } from './overviews.js';
import { openReview } from './review.js';
import { openTypeManager } from './types.js';
import { openAlarms } from './alarms.js';
import { button, clear, el, readLocal, readSession, writeLocal, writeSession } from './utils.js';

const STORAGE = {
    apiBaseUrl: 'lexicon.web.apiBaseUrl',
    theme: 'lexicon.web.theme',
    token: 'lexicon.web.token',
    username: 'lexicon.web.username',
};

function configuredBaseUrl() {
    const stored = readLocal(STORAGE.apiBaseUrl, '');
    if (stored) return stored;
    const config = window.LEXICON_CONFIG || {};
    if (config.apiBaseUrl) return config.apiBaseUrl;
    // A sensible default for a server running on this machine.
    return `${window.location.protocol}//${window.location.hostname}:8628`;
}

function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    writeLocal(STORAGE.theme, theme);
}

class Application {
    constructor() {
        this.loginView = document.getElementById('login-view');
        this.mainView = document.getElementById('main-view');
        this.menuBar = document.getElementById('menu-bar');
        this.sessionLabel = document.getElementById('session-label');
        this.logoutButton = document.getElementById('logout-button');
        this.menuToggle = document.getElementById('menu-toggle');
        this.view = null;
        this.username = '';
    }

    async start() {
        applyTheme(readLocal(STORAGE.theme, 'dark'));
        this.buildLoginForm();
        this.buildMenus();
        api.onUnauthorized = () => this.handleSessionLost();

        const token = readSession(STORAGE.token);
        const baseUrl = configuredBaseUrl();
        api.setBaseUrl(baseUrl);
        if (token) {
            api.setToken(token);
            try {
                const me = await api.me();
                // The server may have been upgraded while this tab was open.
                if (me.apiVersion !== API_VERSION) {
                    writeSession(STORAGE.token, null);
                    api.setToken(null);
                    this.showLogin(`This client speaks API version ${API_VERSION}, but the `
                        + `server reports version ${me.apiVersion}.`);
                    return;
                }
                this.username = me.username;
                await this.showMain();
                return;
            } catch (error) {
                writeSession(STORAGE.token, null);
                api.setToken(null);
            }
        }
        this.showLogin();
    }

    // --- Login -----------------------------------------------------------
    buildLoginForm() {
        const form = document.getElementById('login-form');
        this.serverInput = document.getElementById('login-server');
        this.usernameInput = document.getElementById('login-username');
        this.passwordInput = document.getElementById('login-password');
        this.loginError = document.getElementById('login-error');
        this.loginButton = document.getElementById('login-submit');
        this.serverInput.value = configuredBaseUrl();
        form.addEventListener('submit', (event) => {
            event.preventDefault();
            this.login();
        });
    }

    showLoginError(message) {
        this.loginError.textContent = message || '';
        this.loginError.hidden = !message;
    }

    async login() {
        const baseUrl = this.serverInput.value.trim();
        const username = this.usernameInput.value;
        const password = this.passwordInput.value;
        if (!baseUrl || !username || !password) {
            this.showLoginError('Enter the server URL, user name and password.');
            return;
        }
        this.loginButton.disabled = true;
        this.showLoginError('');
        api.setBaseUrl(baseUrl);
        const mixedContent = api.mixedContentProblem();
        if (mixedContent) {
            this.showLoginError(mixedContent);
            this.loginButton.disabled = false;
            return;
        }
        try {
            // The static frontend and the server are deployed independently, so
            // the API version is checked before anything else.
            const health = await api.health();
            if (health.apiVersion !== API_VERSION) {
                this.showLoginError(
                    `This Lexicon web client speaks API version ${API_VERSION}, but the server `
                    + `at ${baseUrl} reports version ${health.apiVersion}. Update the client or `
                    + 'the server so the versions match.');
                return;
            }
            const session = await api.login(username, password);
            this.username = session.username || username;
            writeSession(STORAGE.token, session.token);
            writeSession(STORAGE.username, this.username);
            writeLocal(STORAGE.apiBaseUrl, api.baseUrl);
            this.passwordInput.value = '';
            await this.showMain();
        } catch (error) {
            this.showLoginError(error instanceof ApiError ? error.message : String(error));
        } finally {
            this.loginButton.disabled = false;
        }
    }

    handleSessionLost() {
        // Called once when a request returns 401: drop the session, return to
        // the login screen, and never retry in a loop.
        writeSession(STORAGE.token, null);
        api.setToken(null);
        this.showLogin('Your session has expired. Sign in again.');
    }

    showLogin(message = '') {
        this.loginView.hidden = false;
        this.mainView.hidden = true;
        this.menuBar.hidden = true;
        this.showLoginError(message);
        this.serverInput.value = api.baseUrl || configuredBaseUrl();
        this.usernameInput.value = readSession(STORAGE.username) || '';
        window.setTimeout(() => {
            (this.usernameInput.value ? this.passwordInput : this.usernameInput).focus();
        }, 0);
    }

    async showMain() {
        this.loginView.hidden = true;
        this.mainView.hidden = false;
        this.menuBar.hidden = false;
        this.sessionLabel.textContent = `${this.username} @ ${api.baseUrl}`;
        setDraftOwner(api.baseUrl, this.username);
        this.view = new MainView(this.mainView);
        try {
            await this.view.refreshAll();
            await this.view.resumeDraft();
        } catch (error) {
            if (!(error instanceof ApiError) || !error.isUnauthorized) {
                await errorDialog(error.message || String(error));
            }
        }
    }

    async logout() {
        try {
            await api.logout();
        } catch (error) {
            // A server that is already gone still ends the local session.
        }
        writeSession(STORAGE.token, null);
        // Signing out leaves no item text behind in this browser.
        clearAllDrafts();
        setDraftOwner('', '');
        this.username = '';
        clear(this.mainView);
        this.view = null;
        this.showLogin('You are signed out.');
    }

    // --- Menus -----------------------------------------------------------
    buildMenus() {
        const definitions = [
            {
                label: 'File',
                entries: [
                    { label: 'Refresh', action: () => this.view && this.view.refreshAll() },
                    { separator: true },
                    { label: 'Export...', action: () => exportDictionary() },
                    {
                        label: 'Import...',
                        action: async () => {
                            if (await importDictionary()) await this.view.refreshAll();
                        },
                    },
                    { separator: true },
                    { label: 'Logout', action: () => this.logout() },
                ],
            },
            {
                label: 'Manage',
                entries: [
                    {
                        label: 'Groups...',
                        action: async () => {
                            if (await openGroupManager()) await this.view.refreshAll();
                        },
                    },
                    {
                        label: 'Types...',
                        action: async () => {
                            if (await openTypeManager()) await this.view.refreshAll();
                        },
                    },
                    { separator: true },
                    { label: 'Alarms...', action: () => openAlarms() },
                ],
            },
            {
                label: 'View',
                entries: [
                    {
                        label: 'Review...',
                        action: async () => {
                            if (await openReview(this.view.groups)) await this.view.refreshAll();
                        },
                    },
                    {
                        label: 'Relationship graph...',
                        action: () => this.view && this.view.showGraph(),
                    },
                    { separator: true },
                    { label: 'All tags...', action: () => showValueOverview('tags') },
                    { label: 'All flags...', action: () => showValueOverview('flags') },
                    { label: 'All aliases...', action: () => showValueOverview('aliases') },
                    { separator: true },
                    // The table is the desktop reading; the list is the one
                    // that fits a phone. Automatic follows the screen.
                    {
                        label: 'Table view',
                        action: () => this.view && this.view.setViewPreference('table'),
                    },
                    {
                        label: 'List view',
                        action: () => this.view && this.view.setViewPreference('list'),
                    },
                    {
                        label: 'Automatic view',
                        action: () => this.view && this.view.setViewPreference('auto'),
                    },
                    { separator: true },
                    { label: 'Light mode', action: () => applyTheme('light') },
                    { label: 'Dark mode', action: () => applyTheme('dark') },
                ],
            },
            {
                label: 'Help',
                entries: [
                    {
                        label: 'About Lexicon',
                        action: () => messageDialog('About Lexicon',
                            `Lexicon web client for API version ${API_VERSION}. `
                            + `Connected to ${api.baseUrl}.`),
                    },
                ],
            },
        ];

        const nav = document.getElementById('menu-list');
        clear(nav);
        const closeAll = () => {
            for (const open of nav.querySelectorAll('.menu.open')) open.classList.remove('open');
            for (const trigger of nav.querySelectorAll('.menu-trigger')) {
                trigger.setAttribute('aria-expanded', 'false');
            }
            // On a phone the menu is a drawer over the page; a chosen command
            // closes it instead of leaving it covering the content.
            this.menuBar.classList.remove('menu-open');
            this.menuToggle.setAttribute('aria-expanded', 'false');
        };

        for (const definition of definitions) {
            const popup = el('div', { class: 'menu-popup', role: 'menu' });
            for (const entry of definition.entries) {
                if (entry.separator) {
                    popup.appendChild(el('hr', { class: 'menu-separator' }));
                    continue;
                }
                popup.appendChild(button(entry.label, {
                    class: 'menu-item',
                    role: 'menuitem',
                    onclick: async () => {
                        closeAll();
                        try {
                            await entry.action();
                        } catch (error) {
                            if (!(error instanceof ApiError) || !error.isUnauthorized) {
                                await errorDialog(error.message || String(error));
                            }
                        }
                    },
                }));
            }
            const trigger = button(definition.label, {
                class: 'menu-trigger',
                'aria-haspopup': 'true',
                'aria-expanded': 'false',
            });
            const menu = el('div', { class: 'menu' }, [trigger, popup]);
            trigger.addEventListener('click', (event) => {
                event.stopPropagation();
                // In the phone drawer every menu is already open and its name
                // is a heading: a tap on it must not close the drawer.
                if (this.menuBar.classList.contains('menu-open')) return;
                const open = menu.classList.contains('open');
                closeAll();
                if (!open) {
                    menu.classList.add('open');
                    trigger.setAttribute('aria-expanded', 'true');
                    const first = popup.querySelector('.menu-item');
                    if (first) first.focus();
                }
            });
            nav.appendChild(menu);
        }

        // Menus close on an outside tap and on Escape, so they work on touch
        // devices where there is no hover.
        document.addEventListener('click', closeAll);
        document.addEventListener('keydown', (event) => {
            if (event.key === 'Escape') closeAll();
        });
        this.menuToggle.addEventListener('click', (event) => {
            event.stopPropagation();
            const open = this.menuBar.classList.toggle('menu-open');
            this.menuToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
        });
        this.logoutButton.addEventListener('click', () => this.logout());
    }
}

const application = new Application();
application.start().catch((error) => {
    document.body.appendChild(el('p', {
        class: 'fatal',
        text: `Lexicon could not start: ${error.message || error}`,
    }));
});
