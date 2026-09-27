#include "StudyPlanDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>

namespace {
QString status(lexicon::StudyPlanStatus value) {
    switch (value) {
    case lexicon::StudyPlanStatus::Upcoming: return "Upcoming";
    case lexicon::StudyPlanStatus::OnTrack: return "On track";
    case lexicon::StudyPlanStatus::Behind: return "Behind";
    case lexicon::StudyPlanStatus::AtRisk: return "At risk";
    case lexicon::StudyPlanStatus::Completed: return "Completed";
    case lexicon::StudyPlanStatus::Overdue: return "Overdue";
    }
    return {};
}
QString typeName(lexicon::StudyPlanType type) {
    const QStringList names{"Book", "Course", "Lesson", "Documentation", "Article", "Video", "Practice", "Other"};
    return names.value(static_cast<int>(type));
}
QString unitName(const lexicon::StudyPlanRecord& plan, int count = 2) {
    const QStringList names{"page", "lesson", "chapter", "section", "module", "video", "exercise", "minute"};
    const QString singular = plan.unitType == lexicon::StudyUnitType::Other
        ? qtbridge::toQt(plan.customUnit) : names.value(static_cast<int>(plan.unitType));
    return count == 1 ? singular : singular + "s";
}
QString range(const lexicon::StudyPlanRecord& plan, int first, int last) {
    if (!first || !last) return "No units scheduled today";
    return QString("%1 %2%3").arg(unitName(plan, last - first + 1)).arg(first)
        .arg(first == last ? QString{} : QString("–%1").arg(last));
}
QString pace(double value) { return QString::number(value, 'f', 2); }
QString requiredPace(const lexicon::StudyPlanOverview& value, const lexicon::StudyPlanRecord& plan) {
    if (!value.requiredUnitsPerRemainingStudyDay)
        return value.ended ? "N/A — deadline passed" : "N/A — no study days remaining";
    return pace(*value.requiredUnitsPerRemainingStudyDay) + " " + unitName(plan) + "/day";
}
QString expectedRange(const lexicon::StudyPlanOverview& value) {
    if (!value.active) return "—";
    if (!value.studyDay) return "No study scheduled";
    return range(value.plan, value.expectedUnitStart.value_or(0), value.expectedUnitEnd.value_or(0));
}
QString difference(const lexicon::StudyPlanOverview& value) {
    if (value.deficitUnits > 0) return QString("Behind by: %1 %2").arg(value.deficitUnits).arg(unitName(value.plan, value.deficitUnits));
    if (value.deficitUnits < 0) return QString("Ahead by: %1 %2").arg(-value.deficitUnits).arg(unitName(value.plan, -value.deficitUnits));
    return "On expected progress";
}
void clearLayout(QLayout* layout) {
    while (auto* child = layout->takeAt(0)) {
        if (child->widget()) delete child->widget();
        if (child->layout()) { clearLayout(child->layout()); delete child->layout(); }
        delete child;
    }
}
} // namespace

StudyPlanDialog::StudyPlanDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Study Plan");
    resize(1040, 740);
    auto* root = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMaximumHeight(370);
    auto* dashboardWidget = new QWidget(scroll);
    m_dashboard = new QVBoxLayout(dashboardWidget);
    scroll->setWidget(dashboardWidget);
    root->addWidget(new QLabel("Study Plan overview", this));
    root->addWidget(scroll);
    auto* heading = new QHBoxLayout;
    heading->addWidget(new QLabel("Study Plans", this));
    heading->addStretch();
    m_filter = new QComboBox(this);
    m_filter->addItems({"All", "Active", "Upcoming", "Finished"});
    m_filter->setObjectName("studyPlanFilter");
    heading->addWidget(m_filter);
    root->addLayout(heading);
    m_table = new QTableWidget(0, 10, this);
    m_table->setObjectName("studyPlanTable");
    m_table->setHorizontalHeaderLabels({"Item", "Group", "Type", "Completed", "Units", "Start", "End", "Planned/day", "Required now", "Status"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    root->addWidget(m_table, 1);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("Add...", this);
    m_edit = new QPushButton("Edit...", this);
    m_edit->setObjectName("studyPlanEdit");
    auto* progress = new QPushButton("Update progress...", this);
    m_delete = new QPushButton("Delete", this);
    m_delete->setObjectName("studyPlanDelete");
    auto* close = new QPushButton("Close", this);
    buttons->addWidget(add); buttons->addWidget(m_edit); buttons->addWidget(progress); buttons->addWidget(m_delete);
    buttons->addStretch(); buttons->addWidget(close);
    root->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] { editPlan({}); });
    connect(m_edit, &QPushButton::clicked, this, [this] { if (auto* plan = selectedPlan()) editPlan(*plan); });
    connect(progress, &QPushButton::clicked, this, [this] { if (auto* plan = selectedPlan()) updateProgress(*plan); });
    connect(m_delete, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { if (auto* plan = selectedPlan()) editPlan(*plan); });
    connect(m_filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { reload(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this, progress] {
        const bool chosen = selectedPlan() != nullptr;
        m_edit->setEnabled(chosen); m_delete->setEnabled(chosen); progress->setEnabled(chosen);
    });
    reload();
}

int StudyPlanDialog::selectedId() const {
    if (!m_table->currentItem()) return -1;
    return m_table->item(m_table->currentRow(), 0)->data(Qt::UserRole).toInt();
}
lexicon::StudyPlanRecord* StudyPlanDialog::selectedPlan() {
    const int id = selectedId();
    for (auto& value : m_values) if (value.plan.id == id) return &value.plan;
    return nullptr;
}
void StudyPlanDialog::reload() {
    auto result = services().core.studyPlans.overview(qtbridge::toCore(QDate::currentDate().toString("yyyy-MM-dd")));
    if (!result) { QMessageBox::critical(this, "Study Plan", qtbridge::toQt(result.error().message)); return; }
    const int previous = selectedId();
    m_values = std::move(*result);
    clearLayout(m_dashboard);
    std::vector<lexicon::StudyPlanOverview> active, upcoming, finished;
    for (const auto& value : m_values) {
        if (value.complete || value.ended) finished.push_back(value);
        else if (value.upcoming) upcoming.push_back(value);
        else active.push_back(value);
    }
    const auto priority = [](lexicon::StudyPlanStatus value) {
        switch (value) {
        case lexicon::StudyPlanStatus::AtRisk: return 0;
        case lexicon::StudyPlanStatus::Behind: return 1;
        case lexicon::StudyPlanStatus::OnTrack: return 2;
        case lexicon::StudyPlanStatus::Completed: return 3;
        default: return 4;
        }
    };
    std::sort(active.begin(), active.end(), [&](const auto& a, const auto& b) {
        if (priority(a.status) != priority(b.status)) return priority(a.status) < priority(b.status);
        return a.plan.item < b.plan.item;
    });
    const auto addHeading = [this](const QString& title) { m_dashboard->addWidget(new QLabel(title, this)); };
    addHeading(QString("Active (%1)").arg(active.size()));
    if (active.empty()) m_dashboard->addWidget(new QLabel("No active plans today.", this));
    std::vector<lexicon::StudyPlanOverview> cards = active;
    cards.insert(cards.end(), upcoming.begin(), upcoming.end());
    cards.insert(cards.end(), finished.begin(), finished.end());
    for (std::size_t index = 0; index < cards.size(); ++index) {
        if (index == active.size()) addHeading(QString("Upcoming (%1)").arg(upcoming.size()));
        if (index == active.size() + upcoming.size()) addHeading(QString("Finished (%1)").arg(finished.size()));
        const auto& value = cards[index];
        const auto& p = value.plan;
        auto* box = new QGroupBox(qtbridge::toQt(p.item), this);
        auto* layout = new QVBoxLayout(box);
        auto* summary = new QLabel(QString("%1%2 · %3 · %4 – %5")
            .arg(p.group.empty() ? QString() : qtbridge::toQt(p.group) + " · ", typeName(p.type), unitName(p),
            qtbridge::toQt(p.startDate), qtbridge::toQt(p.endDate)), box);
        summary->setTextFormat(Qt::PlainText);
        layout->addWidget(summary);
        layout->addWidget(new QLabel(QString("Current progress: %1").arg(p.currentProgress ? QString("%1 %2").arg(unitName(p, 1)).arg(p.currentProgress) : "0"), box));
        layout->addWidget(new QLabel(QString("Expected progress: %1").arg(value.expectedProgress ? QString("%1 %2").arg(unitName(p, 1)).arg(value.expectedProgress) : "0"), box));
        layout->addWidget(new QLabel(difference(value), box));
        layout->addWidget(new QLabel("Expected unit range today: " + expectedRange(value), box));
        layout->addWidget(new QLabel("Recommended today: " + (value.recommendedFirst ? range(p, value.recommendedFirst, value.recommendedLast) : QString("—")), box));
        layout->addWidget(new QLabel(QString("Planned pace: %1 %2/day").arg(pace(value.plannedUnitsPerStudyDay), unitName(p)), box));
        layout->addWidget(new QLabel("Required now: " + requiredPace(value, p), box));
        auto* state = new QLabel(QString("Status: %1").arg(status(value.status)), box);
        if (value.status == lexicon::StudyPlanStatus::AtRisk || value.status == lexicon::StudyPlanStatus::Behind ||
            value.status == lexicon::StudyPlanStatus::Overdue) state->setStyleSheet("color: #b3261e; font-weight: bold;");
        layout->addWidget(state);
        if (!p.note.empty()) { auto* note = new QLabel(qtbridge::toQt(p.note), box); note->setWordWrap(true); layout->addWidget(note); }
        auto* actions = new QHBoxLayout;
        auto* update = new QPushButton("Update progress...", box);
        auto* today = new QPushButton("Mark today complete", box);
        auto* complete = new QPushButton("Mark plan complete", box);
        auto* edit = new QPushButton("Edit...", box);
        today->setEnabled(value.active && !value.complete && value.studyDay && value.expectedUnitEnd &&
            std::max(value.expectedUnitEnd.value_or(0), value.recommendedLast) > p.currentProgress);
        complete->setVisible(!value.complete);
        actions->addWidget(update); actions->addWidget(today); actions->addWidget(complete); actions->addWidget(edit);
        layout->addLayout(actions);
        connect(update, &QPushButton::clicked, this, [this, p] { updateProgress(p); });
        connect(today, &QPushButton::clicked, this, [this, p, value] {
            setProgress(p, std::max({p.currentProgress, value.todayLast, value.recommendedLast}));
        });
        connect(complete, &QPushButton::clicked, this, [this, p] {
            if (QMessageBox::question(this, "Complete Study Plan", QString("Mark \"%1\" complete?").arg(qtbridge::toQt(p.item))) == QMessageBox::Yes)
                setProgress(p, p.lastUnit);
        });
        connect(edit, &QPushButton::clicked, this, [this, p] { editPlan(p); });
        m_dashboard->addWidget(box);
    }
    m_dashboard->addStretch();
    std::vector<lexicon::StudyPlanOverview> visible;
    for (const auto& value : m_values) {
        if (m_filter->currentIndex() == 1 && (!value.active || value.complete)) continue;
        if (m_filter->currentIndex() == 2 && (!value.upcoming || value.complete)) continue;
        if (m_filter->currentIndex() == 3 && !(value.ended || value.complete)) continue;
        visible.push_back(value);
    }
    m_table->setRowCount(static_cast<int>(visible.size()));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto& value = visible[static_cast<std::size_t>(row)];
        const auto& p = value.plan;
        const QStringList cells{qtbridge::toQt(p.item), qtbridge::toQt(p.group), typeName(p.type),
            QString("%1 / %2").arg(value.completedUnits).arg(value.totalUnits),
            unitName(p), qtbridge::toQt(p.startDate), qtbridge::toQt(p.endDate),
            pace(value.plannedUnitsPerStudyDay), value.requiredUnitsPerRemainingStudyDay
                ? pace(*value.requiredUnitsPerRemainingStudyDay) : "N/A", status(value.status)};
        for (int col = 0; col < cells.size(); ++col) {
            auto* cell = new QTableWidgetItem(cells[col]);
            if (col == 0) cell->setData(Qt::UserRole, p.id);
            if (col == 8) cell->setToolTip(requiredPace(value, p));
            m_table->setItem(row, col, cell);
        }
        if (p.id == previous) m_table->selectRow(row);
    }
}
void StudyPlanDialog::setProgress(lexicon::StudyPlanRecord plan, int progress) {
    plan.currentProgress = progress;
    auto saved = services().core.studyPlans.save(plan);
    if (!saved) { QMessageBox::warning(this, "Study Plan", qtbridge::toQt(saved.error().message)); return; }
    reload();
}
void StudyPlanDialog::updateProgress(const lexicon::StudyPlanRecord& plan) {
    QDialog dialog(this);
    dialog.setWindowTitle("Update progress");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Last completed unit:", &dialog));
    auto* progress = new QSpinBox(&dialog);
    progress->setObjectName("studyPlanQuickProgress");
    progress->setRange(plan.firstUnit - 1, plan.lastUnit);
    progress->setSpecialValueText("Not started (0)");
    progress->setValue(plan.currentProgress == 0 ? plan.firstUnit - 1 : plan.currentProgress);
    layout->addWidget(progress);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        setProgress(plan, progress->value() == plan.firstUnit - 1 ? 0 : progress->value());
}
void StudyPlanDialog::removeSelected() {
    auto* plan = selectedPlan();
    if (!plan) return;
    if (QMessageBox::question(this, "Delete Study Plan", QString("Delete study plan \"%1\"?").arg(qtbridge::toQt(plan->item))) != QMessageBox::Yes) return;
    auto removed = services().core.studyPlans.remove(plan->id);
    if (!removed) { QMessageBox::warning(this, "Study Plan", qtbridge::toQt(removed.error().message)); return; }
    reload();
}
void StudyPlanDialog::editPlan(lexicon::StudyPlanRecord plan) {
    QDialog dialog(this);
    dialog.setWindowTitle(plan.id < 0 ? "Add Study Plan" : "Edit Study Plan");
    dialog.resize(560, 650);
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* item = new QLineEdit(qtbridge::toQt(plan.item), &dialog);
    item->setObjectName("studyPlanItem");
    auto* group = new QLineEdit(qtbridge::toQt(plan.group), &dialog);
    group->setObjectName("studyPlanGroup");
    auto* type = new QComboBox(&dialog);
    for (int i = 0; i <= static_cast<int>(lexicon::StudyPlanType::Other); ++i)
        type->addItem(typeName(static_cast<lexicon::StudyPlanType>(i)), i);
    type->setCurrentIndex(static_cast<int>(plan.type));
    auto* unit = new QComboBox(&dialog);
    const QStringList units{"Page", "Lesson", "Chapter", "Section", "Module", "Video", "Exercise", "Minute", "Other"};
    for (int i = 0; i < units.size(); ++i) unit->addItem(units[i], i);
    unit->setCurrentIndex(static_cast<int>(plan.unitType));
    auto* custom = new QLineEdit(qtbridge::toQt(plan.customUnit), &dialog);
    auto showCustom = [unit, custom] { custom->setEnabled(unit->currentIndex() == static_cast<int>(lexicon::StudyUnitType::Other)); };
    QObject::connect(unit, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, showCustom);
    showCustom();
    auto* first = new QSpinBox(&dialog); first->setObjectName("studyPlanEditorFirst");
    first->setRange(1, INT_MAX); first->setValue(plan.firstUnit);
    auto* last = new QSpinBox(&dialog); last->setObjectName("studyPlanEditorLast");
    last->setRange(1, INT_MAX); last->setValue(plan.lastUnit);
    auto* progress = new QSpinBox(&dialog);
    progress->setObjectName("studyPlanEditorProgress");
    progress->setRange(plan.firstUnit - 1, plan.lastUnit);
    progress->setSpecialValueText("Not started (0)");
    progress->setValue(plan.currentProgress == 0 ? plan.firstUnit - 1 : plan.currentProgress);
    const auto updateProgressRange = [first, last, progress] {
        const bool notStarted = progress->value() == progress->minimum();
        const int sentinel = first->value() - 1;
        progress->setRange(sentinel, std::max(sentinel, last->value()));
        if (notStarted) progress->setValue(sentinel);
    };
    connect(first, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, updateProgressRange);
    connect(last, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, updateProgressRange);
    auto* start = new QDateEdit(&dialog); start->setCalendarPopup(true); start->setDisplayFormat("yyyy-MM-dd");
    start->setDate(plan.startDate.empty() ? QDate::currentDate() : QDate::fromString(qtbridge::toQt(plan.startDate), "yyyy-MM-dd"));
    auto* end = new QDateEdit(&dialog); end->setCalendarPopup(true); end->setDisplayFormat("yyyy-MM-dd");
    end->setDate(plan.endDate.empty() ? QDate::currentDate() : QDate::fromString(qtbridge::toQt(plan.endDate), "yyyy-MM-dd"));
    auto* daysWidget = new QWidget(&dialog); auto* days = new QHBoxLayout(daysWidget);
    const QStringList dayNames{"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    std::vector<QCheckBox*> checks;
    for (int i = 0; i < 7; ++i) {
        auto* check = new QCheckBox(dayNames[i], daysWidget); check->setChecked(plan.studyDaysMask & (1 << i));
        checks.push_back(check); days->addWidget(check);
    }
    auto* note = new QPlainTextEdit(qtbridge::toQt(plan.note), &dialog);
    form->addRow("Item:", item); form->addRow("Group:", group); form->addRow("Type:", type); form->addRow("Unit:", unit);
    form->addRow("Custom unit:", custom); form->addRow("First unit:", first); form->addRow("Last unit:", last);
    form->addRow("Last completed (0 = none):", progress); form->addRow("Start:", start); form->addRow("End:", end);
    form->addRow("Study days:", daysWidget); form->addRow("Note:", note);
    root->addLayout(form);
    auto* error = new QLabel(&dialog); error->setStyleSheet("color: #b3261e;"); error->setWordWrap(true); root->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
        plan.item = qtbridge::toCore(item->text().trimmed());
        plan.group = qtbridge::toCore(group->text().trimmed());
        plan.type = static_cast<lexicon::StudyPlanType>(type->currentIndex());
        plan.unitType = static_cast<lexicon::StudyUnitType>(unit->currentIndex());
        plan.customUnit = qtbridge::toCore(custom->text().trimmed());
        plan.firstUnit = first->value(); plan.lastUnit = last->value();
        plan.currentProgress = progress->value() == plan.firstUnit - 1 ? 0 : progress->value();
        plan.startDate = qtbridge::toCore(start->date().toString("yyyy-MM-dd"));
        plan.endDate = qtbridge::toCore(end->date().toString("yyyy-MM-dd"));
        plan.studyDaysMask = 0;
        for (int i = 0; i < 7; ++i) if (checks[i]->isChecked()) plan.studyDaysMask |= 1 << i;
        plan.note = qtbridge::toCore(note->toPlainText());
        auto saved = services().core.studyPlans.save(plan);
        if (!saved) { error->setText(qtbridge::toQt(saved.error().message)); return; }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) reload();
}
