import { api } from './api.js';
import { confirmDialog, errorDialog, field, openDialog } from './dialogs.js';
import { button, clear, el } from './utils.js';

const TYPES = ['Book', 'Course', 'Lesson', 'Documentation', 'Article', 'Video', 'Practice', 'Other'];
const UNITS = ['Page', 'Lesson', 'Chapter', 'Section', 'Module', 'Video', 'Exercise', 'Minute', 'Other'];
const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

export function localStudyDate(now = new Date()) {
    return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
}
export function pace(value) { return Number(value).toFixed(2); }
export function unitLabel(plan, count) {
    const singular = plan.unitType === 'Other' ? plan.customUnit.trim() : plan.unitType.toLowerCase();
    return count === 1 ? singular : `${singular}s`;
}
export function studyRange(plan, first, last) {
    if (!first || !last) return 'No units scheduled today';
    return `${unitLabel(plan, last - first + 1)} ${first}${first === last ? '' : `–${last}`}`;
}
export function expectedRangeText(value) {
    if (!value.active) return '—';
    if (!value.studyDay) return 'No study scheduled';
    return studyRange(value.plan, value.expectedUnitStart, value.expectedUnitEnd);
}
export function requiredPaceText(value) {
    if (value.requiredUnitsPerRemainingStudyDay == null)
        return value.ended ? 'N/A — deadline passed' : 'N/A — no study days remaining';
    return `${pace(value.requiredUnitsPerRemainingStudyDay)} ${unitLabel(value.plan, 2)}/day`;
}
export function differenceText(value) {
    const { deficitUnits, plan } = value;
    if (deficitUnits > 0) return `Behind by: ${deficitUnits} ${unitLabel(plan, deficitUnits)}`;
    if (deficitUnits < 0) return `Ahead by: ${-deficitUnits} ${unitLabel(plan, -deficitUnits)}`;
    return 'On expected progress';
}
export function canMarkToday(value) {
    return value.active && !value.complete && value.studyDay && value.expectedUnitEnd != null &&
        Math.max(value.expectedUnitEnd ?? 0, value.recommendedLast) > value.plan.currentProgress;
}

function select(options, value) {
    const control = el('select');
    for (const option of options) control.append(el('option', { value: option, text: option }));
    control.value = value;
    return control;
}
function numberInput(value, min = 0) {
    return el('input', { type: 'number', step: '1', min: String(min), max: '2147483647', value: String(value) });
}
async function editPlan(plan = {}) {
    const item = el('input', { type: 'text', value: plan.item || '', required: true });
    const type = select(TYPES, plan.type || 'Book');
    const unitType = select(UNITS, plan.unitType || 'Page');
    const customUnit = el('input', { type: 'text', value: plan.customUnit || '', placeholder: 'e.g. kata' });
    const customRow = field('Custom unit:', customUnit);
    const showCustom = () => { customRow.hidden = unitType.value !== 'Other'; };
    unitType.addEventListener('change', showCustom);
    showCustom();
    const firstUnit = numberInput(plan.firstUnit ?? 1, 1);
    const lastUnit = numberInput(plan.lastUnit ?? 1, 1);
    const currentProgress = numberInput(plan.currentProgress ?? 0, 0);
    const startDate = el('input', { type: 'date', value: plan.startDate || localStudyDate() });
    const endDate = el('input', { type: 'date', value: plan.endDate || localStudyDate() });
    const dayChecks = DAYS.map((day, index) => el('label', { class: 'study-day' }, [
        el('input', { type: 'checkbox', checked: Boolean((plan.studyDaysMask ?? 127) & (1 << index)) }), day,
    ]));
    const note = el('textarea', { rows: '4' });
    note.value = plan.note || '';
    return openDialog({
        title: plan.id ? 'Edit Study Plan' : 'Add Study Plan', wide: true, initialFocus: item,
        body: el('div', { class: 'study-editor' }, [
            field('Item:', item), field('Type:', type), field('Unit:', unitType), customRow,
            field('First unit:', firstUnit), field('Last unit:', lastUnit),
            field('Last completed unit (0 = not started):', currentProgress),
            field('Start date:', startDate), field('End date:', endDate),
            el('div', { class: 'study-weekdays' }, [el('strong', { text: 'Study days' }), ...dayChecks]),
            field('Note:', note),
        ]),
        onAccept: async ({ fail }) => {
            const mask = dayChecks.reduce((sum, label, index) => sum + (label.querySelector('input').checked ? (1 << index) : 0), 0);
            const values = { item: item.value.trim(), type: type.value, unitType: unitType.value,
                customUnit: customUnit.value.trim(), firstUnit: Number(firstUnit.value), lastUnit: Number(lastUnit.value),
                currentProgress: Number(currentProgress.value), startDate: startDate.value,
                endDate: endDate.value, studyDaysMask: mask, note: note.value };
            if (!values.item || !mask || (values.unitType === 'Other' && !values.customUnit)) {
                fail('Enter an item, at least one study day, and a custom label for Other.'); return undefined;
            }
            if (!Number.isInteger(values.currentProgress) ||
                (values.currentProgress !== 0 && (values.currentProgress < values.firstUnit || values.currentProgress > values.lastUnit))) {
                fail(`Enter 0 or a unit from ${values.firstUnit} to ${values.lastUnit}.`); return undefined;
            }
            return plan.id ? api.updateStudyPlan(plan.id, values) : api.createStudyPlan(values);
        },
    });
}

export async function openStudyPlans() {
    const content = el('div', { class: 'study-plans' });
    let date = localStudyDate();
    let overviews = [];
    const refresh = async () => {
        date = localStudyDate();
        overviews = await api.studyPlanOverview(date);
        render();
    };
    const change = async (plan, progress) => {
        try { await api.updateStudyPlan(plan.id, { ...plan, currentProgress: progress }); await refresh(); }
        catch (error) { await errorDialog(error.message); }
    };
    const updateProgress = async (plan) => {
        const input = numberInput(plan.currentProgress, 0);
        const result = await openDialog({ title: `Update progress · ${plan.item}`,
            body: field(`Last completed ${unitLabel(plan, 1)} (0 = not started):`, input),
            initialFocus: input, onAccept: async ({ fail }) => {
                const value = Number(input.value);
                if (!Number.isInteger(value) || (value !== 0 && (value < plan.firstUnit || value > plan.lastUnit))) {
                    fail(`Enter 0 or a unit from ${plan.firstUnit} to ${plan.lastUnit}.`); return undefined;
                }
                return value;
            } });
        if (result !== null) await change(plan, result);
    };
    const edit = async (plan) => {
        try { if (await editPlan(plan)) await refresh(); }
        catch (error) { await errorDialog(error.message); }
    };
    const remove = async (plan) => {
        if (!await confirmDialog('Delete Study Plan', `Delete study plan "${plan.item}"?`, { danger: true })) return;
        try { await api.deleteStudyPlan(plan.id); await refresh(); }
        catch (error) { await errorDialog(error.message); }
    };
    function card(value) {
        const p = value.plan;
        const warning = value.status === 'At risk' || value.status === 'Behind' || value.status === 'Overdue';
        const lines = [
            el('h3', { text: p.item }),
            el('p', { class: 'hint', text: `${p.type} · ${unitLabel(p, 2)} · ${p.startDate} – ${p.endDate}` }),
            el('p', { text: `Current progress: ${p.currentProgress ? `${unitLabel(p, 1)} ${p.currentProgress}` : '0'}` }),
            el('p', { text: `Expected progress: ${value.expectedProgress ? `${unitLabel(p, 1)} ${value.expectedProgress}` : '0'}` }),
            el('p', { text: differenceText(value) }),
            el('p', { text: `Expected unit range today: ${expectedRangeText(value)}` }),
        ];
        lines.push(el('p', { text: `Recommended today: ${value.recommendedFirst ? studyRange(p, value.recommendedFirst, value.recommendedLast) : '—'}` }));
        lines.push(el('p', { text: `Planned pace: ${pace(value.plannedUnitsPerStudyDay)} ${unitLabel(p, 2)}/day` }));
        lines.push(el('p', { text: `Required now: ${requiredPaceText(value)}` }));
        lines.push(el('p', { class: warning ? 'study-warning' : 'study-status',
            text: `Status: ${value.status}` }));
        if (p.note) lines.push(el('p', { class: 'study-note', text: p.note }));
        lines.push(el('div', { class: 'study-actions' }, [
            button('Update progress', { class: 'secondary', onclick: () => updateProgress(p) }),
            button('Mark today complete', { class: 'secondary', disabled: !canMarkToday(value),
                onclick: () => { if (canMarkToday(value)) change(p, Math.max(p.currentProgress, value.expectedUnitEnd ?? 0, value.recommendedLast)); } }),
            ...(!value.complete ? [button('Mark plan complete', { class: 'secondary',
                onclick: async () => { if (await confirmDialog('Complete Study Plan', `Mark "${p.item}" complete?`)) await change(p, p.lastUnit); } }),
            ] : []),
            button('Edit', { class: 'secondary', onclick: () => edit(p) }),
            button('Delete', { class: 'secondary', onclick: () => remove(p) }),
        ]));
        return el('article', { class: `study-card ${warning ? 'study-card-warning' : ''}` }, lines);
    }
    function section(title, values) {
        return el('section', {}, [el('h2', { text: `${title} (${values.length})` }),
            ...(values.length ? values.map(card) : [el('p', { class: 'hint', text: 'No plans here.' })])]);
    }
    function render() {
        clear(content);
        const priority = { 'At risk': 0, Behind: 1, 'On track': 2, Completed: 3 };
        const active = overviews.filter((x) => x.active && !x.complete).sort((a, b) =>
            (priority[a.status] ?? 4) - (priority[b.status] ?? 4) || a.plan.item.localeCompare(b.plan.item));
        const upcoming = overviews.filter((x) => x.upcoming && !x.complete);
        const past = overviews.filter((x) => x.ended || x.complete);
        content.append(el('p', { class: 'hint', text: `Today: ${date} · calendar dates are local to this device` }),
            button('Add Study Plan', { class: 'primary', onclick: () => edit({}) }),
            section('Active', active), section('Upcoming', upcoming), section('Finished', past));
    }
    try { await refresh(); }
    catch (error) { await errorDialog(error.message); return; }
    await openDialog({ title: 'Study Plan', body: content, wide: true, showAccept: false, cancelLabel: 'Close' });
}
