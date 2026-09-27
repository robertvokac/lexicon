#pragma once
#include "ApplicationContext.h"
#include <QDialog>
#include <vector>

class QTableWidget;
class QVBoxLayout;
class QPushButton;
class QComboBox;

class StudyPlanDialog : public QDialog {
public:
    explicit StudyPlanDialog(QWidget* parent = nullptr);
private:
    void reload();
    void editPlan(lexicon::StudyPlanRecord plan);
    void removeSelected();
    void updateProgress(const lexicon::StudyPlanRecord& plan);
    void setProgress(lexicon::StudyPlanRecord plan, int progress);
    int selectedId() const;
    lexicon::StudyPlanRecord* selectedPlan();
    QTableWidget* m_table = nullptr;
    QComboBox* m_filter = nullptr;
    QVBoxLayout* m_dashboard = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_delete = nullptr;
    std::vector<lexicon::StudyPlanOverview> m_values;
};
