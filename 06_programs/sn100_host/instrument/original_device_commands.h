#ifndef SN100_ORIGINAL_DEVICE_COMMANDS_H
#define SN100_ORIGINAL_DEVICE_COMMANDS_H

#include <QByteArray>
#include <QList>
#include <QString>

// 对照原frmDriverSet对应按钮槽及commanddefine.h；三通阀按钮的0/1反向按原版保留。
struct OriginalDeviceButton
{
    QString name;
    QByteArray payload;
};
const QList<OriginalDeviceButton> &originalDeviceButtons();
bool isOriginalFixedCommand(const QByteArray &payload);

#endif
