#ifndef SN100_LIS_INBOX_PAGE_H
#define SN100_LIS_INBOX_PAGE_H

#include "lis_dsr_raw.h"
#include <QDialog>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// New auxiliary viewer, not a reconstruction of an original instrument page.
// Ordinary Qt widgets inherit the owner's style. No disk reads or SQL in this UI.
class LisInboxPage : public QDialog
{
    Q_OBJECT
public:
    explicit LisInboxPage(QWidget *parent = nullptr);
    void setState(bool ready, bool canImport, bool busy, bool closing);
    void setDraft(QString path, QString identity, QString expected, bool committed);
    void showStatus(QString message, bool error = false);
    void showReceipt(qint64 actualId, bool duplicate);
    void showList(LisDsrRaw::Summaries rows, qint64 total, int offset);
    void showDetail(LisDsrRaw::Message actual);
    void finishClose();
signals:
    void fileSelected(QString path);
    void importRequested(QString expectedControl);
    void discardRequested();
    void listRequested(int offset);
    void detailRequested(qint64 actualId);
    void reopenRequested();
    void closeRequested();
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    bool confirmDiscard();
    void updateControls();
    void showSegment(int index);
    QLineEdit *file_, *expected_, *identity_;
    QLabel *status_, *receipt_, *range_, *detailMetadata_, *admission_;
    QPushButton *choose_, *import_, *discard_, *refresh_, *previous_, *next_, *open_, *detail_, *close_;
    QTableWidget *list_, *fields_;
    QPlainTextEdit *raw_, *segmentRaw_;
    QComboBox *segments_;
    LisDsrRaw::Message actual_;
    QString path_;
    int offset_ = 0, shownRows_ = 0;
    qint64 total_ = 0;
    bool ready_ = false, canImport_ = false, busy_ = true, closing_ = false;
    bool identityLocked_ = false, committed_ = false, finalClose_ = false;
};
#endif
