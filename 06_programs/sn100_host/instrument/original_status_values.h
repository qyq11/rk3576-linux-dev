#ifndef SN100_ORIGINAL_STATUS_VALUES_H
#define SN100_ORIGINAL_STATUS_VALUES_H

#include "protocol/status_session.h"
#include <QMap>
#include <QString>
#include <QVector>

namespace OriginalInstrument
{
struct SensorDisplay
{
    QString name;
    QString value;
    bool known = false;
    bool normal = false;
};

// 只包含本次Qj实际上报的值；没有收到的字段不沿用旧值或原构造器的默认正常值。
struct StatusDisplay
{
    QMap<QString, QString> labels;
    QMap<QString, bool> coverNormal;
    QVector<SensorDisplay> sensors;
    bool sn600 = false;
};

// 对应原CommandReceiver::pcQueryStatus及frmDriverSet三个显示函数，不发送设备命令。
StatusDisplay statusDisplay(const Sn100Protocol::StatusSnapshot &snapshot, int role);
}

#endif
