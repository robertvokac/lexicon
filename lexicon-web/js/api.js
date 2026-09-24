// The only place in the web client that talks HTTP. Every request goes through
// request(): it adds the Bearer token, encodes and decodes JSON, converts REST
// errors into ApiError and reports an expired session exactly once.

export const API_VERSION = 1;

export class ApiError extends Error {
    constructor(status, code, message) {
        super(message);
        this.name = 'ApiError';
        this.status = status;
        this.code = code;
    }

    get isUnauthorized() {
        return this.status === 401;
    }
}

const DEFAULT_TIMEOUT_MS = 20000;
const UPLOAD_TIMEOUT_MS = 120000;

function normalizeBaseUrl(url) {
    return (url || '').trim().replace(/\/+$/, '');
}

export class LexiconApi {
    constructor() {
        this.baseUrl = '';
        this.token = null;
        this.onUnauthorized = null;
    }

    setBaseUrl(url) {
        this.baseUrl = normalizeBaseUrl(url);
    }

    setToken(token) {
        this.token = token || null;
    }

    get authenticated() {
        return Boolean(this.token);
    }

    // A page served over HTTPS cannot call a plaintext API: the browser blocks
    // the request as mixed content. Say so instead of failing mysteriously.
    mixedContentProblem() {
        if (!this.baseUrl) return 'The server URL is not configured.';
        let parsed;
        try {
            parsed = new URL(this.baseUrl);
        } catch (error) {
            return 'The server URL is not a valid absolute URL.';
        }
        if (!['http:', 'https:'].includes(parsed.protocol)) {
            return 'The server URL must use http:// or https://.';
        }
        const localHosts = ['localhost', '127.0.0.1', '[::1]', '::1'];
        const isLocal = localHosts.includes(parsed.hostname);
        if (window.location.protocol === 'https:' && parsed.protocol === 'http:' && !isLocal) {
            return 'This page is served over HTTPS, so it cannot use a plain http:// API URL. '
                + 'Use https:// for the Lexicon server, or open this page over http://.';
        }
        return null;
    }

    url(path) {
        return `${this.baseUrl}/api/v1${path}`;
    }

    async request(method, path, options = {}) {
        const { body, contentType, expect = 'json', timeoutMs, headers = {} } = options;
        const mixedContent = this.mixedContentProblem();
        if (mixedContent) throw new ApiError(0, 'configuration', mixedContent);

        const controller = new AbortController();
        const timer = window.setTimeout(
            () => controller.abort(),
            timeoutMs || (expect === 'blob' || body instanceof Blob ? UPLOAD_TIMEOUT_MS : DEFAULT_TIMEOUT_MS),
        );
        const requestHeaders = { Accept: 'application/json', ...headers };
        if (this.token) requestHeaders.Authorization = `Bearer ${this.token}`;
        if (body !== undefined && body !== null) {
            requestHeaders['Content-Type'] = contentType || 'application/json';
        }

        let response;
        try {
            response = await fetch(this.url(path), {
                method,
                headers: requestHeaders,
                body: body === undefined || body === null
                    ? undefined
                    : (typeof body === 'string' || body instanceof Blob ? body : JSON.stringify(body)),
                mode: 'cors',
                credentials: 'omit',
                cache: 'no-store',
                signal: controller.signal,
            });
        } catch (error) {
            if (error.name === 'AbortError') {
                throw new ApiError(0, 'timeout', 'The server did not answer in time.');
            }
            throw new ApiError(0, 'network',
                'Cannot reach the Lexicon server. Check the server URL, that the server is '
                + 'running, and that this origin is in its --allowed-origin list.');
        } finally {
            window.clearTimeout(timer);
        }

        if (response.status === 401) {
            // Drop the session once and return to the login screen. Never retry:
            // that is how request loops start.
            const failure = await this.readError(response);
            if (this.token && this.onUnauthorized) {
                this.token = null;
                this.onUnauthorized(failure);
            }
            this.token = null;
            throw failure;
        }
        if (!response.ok) throw await this.readError(response);

        if (expect === 'none' || response.status === 204) return null;
        if (expect === 'blob') return response.blob();
        const text = await response.text();
        if (!text) return null;
        try {
            return JSON.parse(text);
        } catch (error) {
            throw new ApiError(response.status, 'malformed_response',
                'The server sent a response that is not valid JSON.');
        }
    }

    async readError(response) {
        let code = 'error';
        let message = `The server answered with HTTP ${response.status}.`;
        try {
            const body = await response.json();
            if (body && body.error) {
                code = body.error.code || code;
                message = body.error.message || message;
            }
        } catch (error) {
            // A non-JSON error body (a proxy page, for example) keeps the default.
        }
        if (response.status === 429) {
            const retryAfter = response.headers.get('Retry-After');
            if (retryAfter) message += ` Try again in ${retryAfter} seconds.`;
        }
        return new ApiError(response.status, code, message);
    }

    get(path, options) { return this.request('GET', path, options); }
    post(path, body, options) { return this.request('POST', path, { ...options, body }); }
    put(path, body, options) { return this.request('PUT', path, { ...options, body }); }
    delete(path) { return this.request('DELETE', path, { expect: 'none' }); }

    // Health and session --------------------------------------------------
    health() { return this.get('/health'); }

    async login(username, password) {
        const result = await this.post('/auth/login', { username, password });
        this.setToken(result.token);
        return result;
    }

    async logout() {
        try {
            await this.request('POST', '/auth/logout', { expect: 'none' });
        } finally {
            this.token = null;
        }
    }

    me() { return this.get('/auth/me'); }

    // Groups ---------------------------------------------------------------
    async groups() { return (await this.get('/groups')).groups; }
    async defaultGroupId() { return (await this.get('/groups/default')).groupId; }
    async createGroup(group) { return (await this.post('/groups', group)).group; }
    async updateGroup(id, group) { return (await this.put(`/groups/${id}`, group)).group; }
    deleteGroup(id) { return this.delete(`/groups/${id}`); }

    // Types and fields -----------------------------------------------------
    async types(groupId) {
        const query = groupId && groupId > 0 ? `?groupId=${encodeURIComponent(groupId)}` : '';
        return (await this.get(`/types${query}`)).types;
    }
    async createType(type) { return (await this.post('/types', type)).type; }
    async updateType(id, type) { return (await this.put(`/types/${id}`, type)).type; }
    deleteType(id) { return this.delete(`/types/${id}`); }
    async typeItemCount(id) { return (await this.get(`/types/${id}/item-count`)).count; }

    async fields(typeId) { return (await this.get(`/types/${typeId}/fields`)).fields; }
    async createField(typeId, field) {
        return (await this.post(`/types/${typeId}/fields`, field)).field;
    }
    async updateField(id, field) { return (await this.put(`/fields/${id}`, field)).field; }
    deleteField(id) { return this.delete(`/fields/${id}`); }
    async fieldValueCount(id) { return (await this.get(`/fields/${id}/value-count`)).count; }

    // Items ----------------------------------------------------------------
    queryItems(query) { return this.post('/items/query', query); }
    getItem(id, include) {
        const query = include && include.length ? `?include=${include.join(',')}` : '';
        return this.get(`/items/${id}${query}`);
    }
    createItem(payload) { return this.post('/items', payload); }
    updateItem(id, payload) { return this.put(`/items/${id}`, payload); }
    deleteItem(id) { return this.delete(`/items/${id}`); }
    logItemRead(id) { return this.request('POST', `/items/${id}/read`, { expect: 'none' }); }
    async itemLinks(id) { return (await this.get(`/items/${id}/links`)).links; }
    itemGraph(id, depth = 2, limit = 100) {
        return this.get(`/items/${id}/graph?depth=${depth}&limit=${limit}`);
    }
    async itemBacklinks(id) { return (await this.get(`/items/${id}/backlinks`)).backlinks; }

    async resolveItem(title, disambiguation) {
        const query = new URLSearchParams({ title });
        if (disambiguation) query.set('disambiguation', disambiguation);
        return (await this.get(`/items/resolve?${query.toString()}`)).itemId;
    }

    // Search and usage -----------------------------------------------------
    async suggestions() { return (await this.get('/search/suggestions')).values; }
    async itemTitles() { return (await this.get('/search/item-titles')).values; }
    async tagUsage() { return (await this.get('/usage/tags')).values; }
    async flagUsage() { return (await this.get('/usage/flags')).values; }
    async aliasUsage() { return (await this.get('/usage/aliases')).values; }

    // Blobs ----------------------------------------------------------------
    async uploadBlob(file) {
        return (await this.uploadBlobDetailed(file)).hash;
    }

    // { hash, mediaType }: mediaType names the image type the server sees in
    // the bytes, or is null.
    uploadBlobDetailed(file) {
        return this.request('POST', '/blobs', {
            body: file,
            contentType: 'application/octet-stream',
        });
    }

    downloadBlob(hash) {
        return this.request('GET', `/blobs/${hash}`, { expect: 'blob' });
    }

    // Alarms -----------------------------------------------------------------
    // The soonest first. Times are UTC "YYYY-MM-DDTHH:MM:SSZ".
    async alarms() { return (await this.get('/alarms')).alarms; }
    async createAlarm(alarm) { return (await this.post('/alarms', alarm)).alarm; }
    async updateAlarm(id, alarm) { return (await this.put(`/alarms/${id}`, alarm)).alarm; }
    deleteAlarm(id) { return this.delete(`/alarms/${id}`); }
    // { alarms, now }: those gone off and not dismissed, by the server's clock.
    dueAlarms() { return this.get('/alarms/due'); }
    async dismissAlarm(id) { return (await this.post(`/alarms/${id}/dismiss`, {})).alarm; }
    async snoozeAlarm(id, minutes) { return (await this.post(`/alarms/${id}/snooze`, { minutes })).alarm; }

    // Review -----------------------------------------------------------------
    reviewQueue(groupId, limit = 20) {
        const query = new URLSearchParams({ limit: String(limit) });
        if (groupId > 0) query.set('groupId', String(groupId));
        return this.get(`/review?${query.toString()}`);
    }

    async reviewItem(id, rating) {
        return (await this.post(`/items/${id}/review`, { rating })).item;
    }

    // Cards ------------------------------------------------------------------
    // Questions and answers about an item. The counts are the server's: a
    // card is created and edited with its question and answer only.
    async itemCards(itemId) { return (await this.get(`/items/${itemId}/cards`)).cards; }
    async createCard(itemId, card) { return (await this.post(`/items/${itemId}/cards`, card)).card; }
    async updateCard(id, card) { return (await this.put(`/cards/${id}`, card)).card; }
    deleteCard(id) { return this.delete(`/cards/${id}`); }

    // A quiz answer, Yes (true) or No (false); the answer is the card as it
    // is now, its counts and last attempt by the server's clock.
    async attemptCard(id, success) {
        return (await this.post(`/cards/${id}/attempt`, { success: Boolean(success) })).card;
    }

    // { cards, itemCount, truncated }: depth 0 is the item alone, 1 to 3 its
    // relationship neighbourhood as the graph finds it.
    quizCards(itemId, depth = 0, limit = 150) {
        return this.get(`/items/${itemId}/quiz-cards?depth=${depth}&limit=${limit}`);
    }

    // Export and import ------------------------------------------------------
    exportDictionary(includeFiles) {
        return this.request('GET', `/export?blobs=${includeFiles ? 'true' : 'false'}`, {
            expect: 'blob',
            timeoutMs: UPLOAD_TIMEOUT_MS,
        });
    }

    async importDictionary(file) {
        const result = await this.request('POST', '/import', {
            body: file,
            contentType: 'application/json',
            timeoutMs: UPLOAD_TIMEOUT_MS,
        });
        return result.report;
    }
}

export const api = new LexiconApi();
