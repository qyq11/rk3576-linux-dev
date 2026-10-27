#ifndef SN100_PROJECT_ADVANCED_H
#define SN100_PROJECT_ADVANCED_H

#include <QList>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QStringList>

class QWidget;
class QTableWidget;

// 沿用原字段名，空白和非法草稿保留原文，交给保存校验报告错误。
using ProjectActionFields = QMap<QString, QString>;
struct ProjectAdvanced
{
    bool configured = false;
    ProjectActionFields fields;
    QList<ProjectActionFields> samples;
    QList<ProjectActionFields> reagents;
};
Q_DECLARE_METATYPE(ProjectAdvanced)

// 绑定原版表单，只维护待保存草稿；不访问数据库，也不发送仪器命令。
class ProjectAdvancedEditor : public QObject
{
    Q_OBJECT
public:
    explicit ProjectAdvancedEditor(QWidget *root, QObject *parent = nullptr);
    void showAdvanced(const ProjectAdvanced &value, const QStringList &rateLabels);
    ProjectAdvanced advanced() const;
    void setWritable(bool writable);
    void setReagentChoices(const QStringList &reagents, const QStringList &cleaners);
    void setDiluentChoices(const QStringList &names);
    void setSampleSelections(const QMap<QString, bool> &options, const QString &defaultLabel);
    void setOriginalProfile(bool original);
    QStringList sampleRateLabels() const;
    QMap<QString, bool> sampleSelections() const;
    QString defaultSampleLabel() const;
    QString validateSampleSelections() const;
    static QString validate(const ProjectAdvanced &value, const QStringList &rateLabels,
                            bool originalProfile = false);
    static QStringList scalarFields();
    static QStringList sampleColumns();
    static QStringList reagentColumns();

signals:
    void edited();

private:
    void populateRow(QTableWidget *table, const ProjectActionFields &values, bool sample);
    ProjectActionFields rowFields(QTableWidget *table, int row, bool sample) const;
    void changed();
    void refreshDefaultChoices();
    void refreshDefaultSample();
    QWidget *m_root;
    QTableWidget *m_samples;
    QTableWidget *m_reagents;
    QStringList m_reagentNames;
    QStringList m_cleanerNames;
    QString m_defaultLabel;
    bool m_populating = false;
    bool m_writable = false;
    bool m_configured = false;
    bool m_diluentSpecified = false;
    bool m_sampleActionsPresent = false;
    bool m_originalProfile = false;
};

#endif
