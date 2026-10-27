#ifndef SN100_QUALITY_CONTROL_PAGE_H
#define SN100_QUALITY_CONTROL_PAGE_H

#include "qc_store.h"
#include <QPointer>
#include <QWidget>

namespace Ui { class frmQuality; }
class QDialog;
class QCloseEvent;

// 原质控主页的本地切片。这里只维护控制品、关联项目/规则元数据与批次，
// 不创建仪器任务，不计算临床规则，也不以空曲线代表正常质控。
class QualityControlPage : public QWidget
{
    Q_OBJECT
public:
    explicit QualityControlPage(QcStore *store, QWidget *parent = nullptr);
    ~QualityControlPage() override;
    bool isReady() const { return ready_; }
    bool isBusy() const;
    bool hasWarning() const;
    bool isDirty() const;
    QString statusText() const { return status_; }
    QcControls controls() const { return controls_; }
    bool requestLeave(const QString &action);
    void setProjects(QcProjectRefs projects);
    // 独立测试/统一壳均复用原按钮入口，稳定ID无需依赖过滤后的行号。
    void openManager();
    void openBatches(qint64 controlId = 0);
signals:
    void requestLoad(qint64 requestId);
    void requestCatalog(qint64 requestId, QcProjectRefs projects);
    void requestSaveControl(qint64 requestId, QcControl control);
    void requestRemoveControl(qint64 requestId, qint64 controlId);
    void requestProjects(qint64 requestId, qint64 controlId, QcProjectRefs projects);
    void requestSaveBatch(qint64 requestId, QcBatch batch);
    void requestRemoveBatch(qint64 requestId, qint64 batchId);
    void requestSwitchBatch(qint64 requestId, qint64 controlId, qint64 batchId);
    void operationFinished(QString operation, bool success);
    void statusChanged(QString message, bool error);
    void busyChanged(bool busy);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct DialogState;
    Ui::frmQuality *ui;
    DialogState *dialogs_;
    QcStore *store_;
    QcControls controls_;
    QcProjectRefs projects_;
    qint64 selectedId_ = 0;
    qint64 requestId_ = 0;
    qint64 catalogRequestId_ = 0;
    qint64 selectAfterLoad_ = 0;
    bool ready_ = false;
    bool busy_ = true;
    bool rendering_ = false;
    QString status_;
    QString referenceWarning_;
    QString operation_;
    const QcControl *control(qint64 id) const;
    void reload();
    void render();
    void renderProjects();
    void renderManager();
    void renderBatches();
    void showAddControl();
    void showProjectChooser();
    void setBusy(bool busy);
    void report(const QString &message, bool error = false);
    void showWarning(const QString &message);
    qint64 begin(const QString &operation);
    bool discardDraft(const QString &action);
    void saveManager();
    void rememberRuleDraft();
    void showRuleDraft();
};
#endif
