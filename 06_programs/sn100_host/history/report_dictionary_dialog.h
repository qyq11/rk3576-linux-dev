#ifndef REPORT_DICTIONARY_DIALOG_H
#define REPORT_DICTIONARY_DIALOG_H

#include "../data/hospital_dictionary_query.h"
#include <QDialog>

namespace Ui
{
class MaskDlgSelect;
}

// 只显示字典并返回选中的名称；没有保存患者/编辑字典/设备操作入口。
class ReportDictionaryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReportDictionaryDialog(const QString &type, QWidget *parent = nullptr);
    ~ReportDictionaryDialog() override;
    void openForSelection();

public slots:
    void receiveReply(HospitalDictionaryReply reply);

signals:
    void queryRequested(HospitalDictionaryRequest request);
    void nameSelected(QString name);

protected:
    void done(int result) override;

private:
    void query(const QString &filter);
    void confirmSelection();
    void showTableMessage(const QString &message);

    Ui::MaskDlgSelect *ui;
    QString m_type;
    HospitalDictionaryRequest m_request;
    HospitalDictionaryEntries m_entries;
    bool m_active = false;
    bool m_ready = false;
};

#endif
