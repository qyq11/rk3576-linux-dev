#ifndef SN100_PROJECT_ADVANCED_H
#define SN100_PROJECT_ADVANCED_H

#include <QList>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QStringList>

class QWidget;
class QTableWidget;

// Original field names remain explicit in the allowlists below. Text retains
// empty/invalid drafts; no unchecked QString::toDouble() may turn them into 0.
using ProjectActionFields = QMap<QString, QString>;
struct ProjectAdvanced
{
    bool configured = false;
    ProjectActionFields fields;
    QList<ProjectActionFields> samples;
    QList<ProjectActionFields> reagents;
};
Q_DECLARE_METATYPE(ProjectAdvanced)

// Binds the existing original form, without SQL, commands, calibration or
// algorithms. QObject owns the binder; tables own their replacement editors.
class ProjectAdvancedEditor : public QObject
{
    Q_OBJECT
public:
    explicit ProjectAdvancedEditor(QWidget *root, QObject *parent = nullptr);
    void showAdvanced(const ProjectAdvanced &value, const QStringList &rateLabels);
    ProjectAdvanced advanced() const;
    void setWritable(bool writable);
    void setReagentChoices(const QStringList &reagents, const QStringList &cleaners);
    void setSampleSelections(const QMap<QString, bool> &options, const QString &defaultLabel);
    static QString validate(const ProjectAdvanced &value, const QStringList &rateLabels);
    static QStringList scalarFields();
    static QStringList sampleColumns();
    static QStringList reagentColumns();

signals:
    void edited();

private:
    void populateRow(QTableWidget *table, const ProjectActionFields &values, bool sample);
    ProjectActionFields rowFields(QTableWidget *table, int row, bool sample) const;
    void changed();
    void refreshDefaultSample();
    QWidget *m_root;
    QTableWidget *m_samples;
    QTableWidget *m_reagents;
    QStringList m_rateLabels;
    QStringList m_reagentNames;
    QStringList m_cleanerNames;
    QString m_defaultLabel;
    bool m_populating = false;
    bool m_writable = false;
    bool m_configured = false;
};

#endif
