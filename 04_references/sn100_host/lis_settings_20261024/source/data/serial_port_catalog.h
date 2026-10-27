#ifndef SN100_SERIAL_PORT_CATALOG_H
#define SN100_SERIAL_PORT_CATALOG_H
#include <QStringList>

// 仅查看OS设备目录/注册表元数据；不打开串口，不探测线路或握手。
QStringList localSerialPortNames(QString *error);
#endif
