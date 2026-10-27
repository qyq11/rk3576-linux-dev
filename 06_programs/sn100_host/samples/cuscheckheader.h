#ifndef CUSCHECKHEADER_H
#define CUSCHECKHEADER_H

#include <QHeaderView>

class CusCheckHeader : public QHeaderView
{
    Q_OBJECT
public:
    CusCheckHeader( int checkColumnIndex,Qt::Orientation orientation,QWidget * parent = 0) :
    QHeaderView(orientation, parent)
    {
        m_checkColIdx = checkColumnIndex;
        checkState = Qt::Unchecked;
    }
    void setCheckState(Qt::CheckState boxChecked);

signals:
    void signalCheckStausChange(Qt::CheckState);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const;
    void mousePressEvent(QMouseEvent *event);

private:
    QColor imgColor;
    Qt::CheckState checkState;
    int m_checkColIdx;


};

#endif // CUSCHECKHEADER_H
