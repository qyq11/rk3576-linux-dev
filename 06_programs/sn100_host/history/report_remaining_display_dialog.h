#ifndef SN100_REPORT_REMAINING_DISPLAY_DIALOG_H
#define SN100_REPORT_REMAINING_DISPLAY_DIALOG_H

#include "report_remaining_view.h"
#include <QDialog>

class QListWidget;
namespace Ui { class MaskDlgDisplaySetting; }

class ReportRemainingDisplayDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReportRemainingDisplayDialog(HistoryDisplayColumns columns,
        HistoryDisplayColumns defaults, QWidget *parent = nullptr);
    ~ReportRemainingDisplayDialog() override;
    void setSaving(bool saving);
    void saveFailed(QString message);
    void acceptSaved();
    bool hasUnsavedChanges() const;

signals:
    void saveRequested(HistoryDisplayColumns columns);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void fillLists();
    void fillList(QListWidget *list, int group);
    void restoreDefaultOrder();
    bool moveColumn(const QString &sourceKey, const QString &targetKey, int group);

    Ui::MaskDlgDisplaySetting *ui;
    HistoryDisplayColumns m_columns;
    HistoryDisplayColumns m_defaults;
    HistoryDisplayColumns m_initialColumns;
    bool m_saving = false;
};

#endif
