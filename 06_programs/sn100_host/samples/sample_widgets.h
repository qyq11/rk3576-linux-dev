#ifndef SN100_SAMPLE_WIDGETS_H
#define SN100_SAMPLE_WIDGETS_H
#include "sample_types.h"
#include <QWidget>
#include <QColor>
#include <QStringList>
namespace Ui
{
class CusSampleRack;
}
class SampleRackView : public QWidget
{
    Q_OBJECT
  public:
    SampleRackView(const SampleRack &rack, int index, bool selected, QWidget *parent = nullptr);
    ~SampleRackView() override;
  public slots:
    void setSelectedColour(QColor colour);
  signals:
    void selected(qint64 id);
    void editRequested(qint64 id);
    void renameRequested(qint64 id);

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

  private:
    Ui::CusSampleRack *ui;
    qint64 id_;
    bool selected_ = false;
    QColor selectedColour_ = QColor(QStringLiteral("#B7D7FF"));
};
class SampleTaskView : public QWidget
{
  public:
    explicit SampleTaskView(QWidget *parent = nullptr) : QWidget(parent) {}
    void setNames(QStringList names, QList<SampleTaskState> states = {});
    QList<SampleTaskState> states() const { return states_; }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QStringList names_;
    QList<SampleTaskState> states_;
};
#endif
