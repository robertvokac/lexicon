#pragma once
#include "ApplicationContext.h"
#include <QDialog>
#include <array>
#include <vector>

class QTableWidget;
class QVBoxLayout;
class QPushButton;
class QComboBox;
class QLineEdit;
class QLabel;

class StudyPlanDialog : public QDialog {
public:
    explicit StudyPlanDialog(QWidget* parent = nullptr);
private:
    void reload();
    void renderTable();
    void editPlan(lexicon::StudyPlanRecord plan);
    void removeSelected();
    void updateProgress(const lexicon::StudyPlanRecord& plan);
    void setProgress(lexicon::StudyPlanRecord plan, int progress);
    int selectedId() const;
    lexicon::StudyPlanRecord* selectedPlan();
    QTableWidget* m_table = nullptr;
    QComboBox* m_filter = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_pageLabel = nullptr;
    QPushButton* m_previous = nullptr;
    QPushButton* m_next = nullptr;
    std::array<int, 4> m_pages{};
    std::array<QString, 4> m_queries{};
    QVBoxLayout* m_dashboard = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_delete = nullptr;
    std::vector<lexicon::StudyPlanOverview> m_values;
};
