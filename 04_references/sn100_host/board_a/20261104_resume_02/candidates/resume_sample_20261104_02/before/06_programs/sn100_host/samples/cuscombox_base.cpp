#include "cuscombox_base.h"

#include "qtmaterialflatbutton.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QEvent>
#include <QScroller>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>


void CusCombox_Base::setParent(QWidget *parent)
{
    menu->setParent(parent);
    frame->setParent(parent);
}

CusCombox_Base::CusCombox_Base(QWidget *parent) : QWidget(parent)
{
    menu         = new QListWidget(this);
    menu->setWindowFlags(Qt::Popup);
    frame        = new QWidget(this);

    //    menu->setParent(mainWinPtr);
    //    frame->setParent(mainWinPtr);

    QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(11);
    effect->setColor(QColor(0, 0, 0, 50));
    effect->setOffset(0, 3);

    frame->setGraphicsEffect(effect);
    frame->setVisible(false);
    menu->setVisible(false);

    menu->setSelectionMode(QAbstractItemView::NoSelection);
    menu->setEditTriggers(QAbstractItemView::NoEditTriggers);
    menu->setFocusPolicy(Qt::NoFocus);
    menu->setFrameShape(QListWidget::NoFrame);
    menu->installEventFilter(this);
    frame->installEventFilter(this);

    QScroller *pScrollerRack = QScroller::scroller(menu);
    pScrollerRack->grabGesture(menu,QScroller::LeftMouseButtonGesture);
    menu->setVerticalScrollMode(QListWidget::ScrollPerPixel);

    m_verticalScrollbar = new QtMaterialScrollBar;
    m_verticalScrollbar->setSliderColor(QColor(34, 34, 34, 50));
    m_verticalScrollbar->setBackgroundColor(Qt::transparent);
    m_verticalScrollbar->setCanvasColor(Qt::transparent);
    m_verticalScrollbar->setHideOnMouseOut(true);

    menu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    menu->setVerticalScrollBar(m_verticalScrollbar);

}

CusCombox_Base::~CusCombox_Base()
{
    delete menu;
    delete frame;
}

void CusCombox_Base::setSourceData(QStringList txts)
{
    height_button = 10;
    sourceData = txts;
    if(!sourceData.contains(currentData))
    {
        if(!sourceData.isEmpty())
            currentData = txts.first();
        else
            currentData = "";
    }
    menu->clear();

}

void CusCombox_Base::setCurrentText(QString txt)
{
    currentData = txt;
    update();

}

void CusCombox_Base::setCurrentIndex(int index)
{
    if(index < sourceData.size())
    {
        currentData = sourceData.at(index);
        update();
    }
}

QString CusCombox_Base::getCurrentText()
{
    return currentData;
}

int CusCombox_Base::getCurrentIndex()
{
    return sourceData.indexOf(currentData);
}

void CusCombox_Base::focusOutEvent(QFocusEvent *event)
{
    if(menu->isVisible())
    {
        menu->hide();
    }
    QWidget::focusOutEvent(event);
}

bool CusCombox_Base::eventFilter(QObject *watched, QEvent *event)
{
    if (this->frame == watched)
    {
        switch (event->type())
        {
        case QEvent::Paint: {
            QPainter painter(this->frame);
            painter.fillRect(this->frame->rect(), Qt::white);
            break;
        }
        default:
            break;
        }
    }
    else if (this->menu == watched)
    {
        switch (event->type())
        {
        case QEvent::Show: {
            this->frame->setGeometry(this->menu->geometry());
            this->frame->show();
            this->frame->raise();
            this->menu->raise();
            this->setFocus();
            break;
        }
        case QEvent::Hide: {
            this->frame->hide();
            break;
        }
        default:
            break;
        }
    }
    else
    {
        QtMaterialFlatButton *widget;
        if ((widget = static_cast<QtMaterialFlatButton *>(watched))) {
            if(event->type() == QEvent::MouseButtonPress)
            {
                QString text(widget->text());
                setCurrentText(text);
                emit itemSelected(getCurrentIndex(), getCurrentText());
                this->menu->hide();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CusCombox_Base::mousePressEvent(QMouseEvent *event)
{
    if(!sourceData.isEmpty())
    {
        if(!menu->isVisible())
        {
            this->menu->move(this->mapToGlobal(QPoint(0, 0)) + QPoint(0, height() + 6));
            const QScreen *screen=QGuiApplication::screenAt(mapToGlobal(QPoint(0,height())));
            const int available=screen?screen->availableGeometry().bottom()-menu->y():height_button;
            menu->setFixedSize(this->width(),qMax(50,qMin(available,height_button)));
            menu->show();
        }
        else
        {
            menu->hide();
        }
    }
    QWidget::mousePressEvent(event);
}

void CusCombox_Base::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    drawArrow(&painter);
    drawCurrentText(&painter);
    drawSelectButton();
}

void CusCombox_Base::drawSelectButton()
{
    if(menu->count() != sourceData.size())
    {
        menu->clear();
        for(auto txt:sourceData)
        {
            QFont font("苹方 粗体");
            font.setPixelSize(16);
            QtMaterialFlatButton *item = new QtMaterialFlatButton;
            item->setFont(font);
            item->setTextAlignment(Qt::AlignLeft);
            item->setCornerRadius(0);
            item->setHaloVisible(false);
            item->setFixedHeight(50);
            item->setForegroundColor("#4A4A4A");
            item->setOverlayStyle(Material::TintedOverlay);
            item->installEventFilter(this);
            height_button += 50;

            QFontMetrics fm(font);
            QString text = txt;
            QRect rect = fm.boundingRect(text);
            item->setFixedWidth(qMax(this->width(), rect.width()));

            item->setText(text);
            int row  = menu->count();
            QListWidgetItem *itemList = new QListWidgetItem;
            itemList->setSizeHint(QSize(this->width(),50));
            menu->insertItem(row,itemList);
            menu->setItemWidget(itemList,item);
        }
    }
}

void CusCombox_Base::drawArrow(QPainter *painter)
{
    painter->save();
    QFontMetrics fontMetrics = painter->fontMetrics();
    int textHeight = fontMetrics.height();
    QSvgRenderer svgRender;
    svgRender.load(QString(":/image/arrow_drop_down.svg"));
    //svgRender.render(painter,QRectF(this->width()-this->height()/2,this->height()/4,this->height()/2,this->height()/2));
    svgRender.render(painter,QRectF(this->width() / 2, (this->height()- textHeight)/2, textHeight, textHeight));
    painter->restore();
}

void CusCombox_Base::drawCurrentText(QPainter *painter)
{
    painter->save();
    QRectF textRectf = QRectF(0,0,this->width()/3*2,this->height());
    QColor nameTextColor = "#4A4A4A";
    QFont font("苹方 粗体");
    font.setPixelSize(16);
    painter->setFont(font);
    painter->setPen(nameTextColor);
    painter->drawText(textRectf, Qt::AlignLeft|Qt::AlignVCenter, currentData);//试剂名称
    painter->restore();
}
