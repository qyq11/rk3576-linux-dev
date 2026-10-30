#include "instrument/device_control.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QtTest>
using namespace Sn100Protocol;

class OriginalTaskTransportRkTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("只允许在RK ARM64运行实际TCP传输驱动。");
#endif
    }
    void complete128000ByteTaskArrivesWithoutTruncation()
    {
        QTcpServer server;
        QPointer<QTcpSocket> peer;
        FrameDecoder decoder(Direction::HostToDevice);
        QElapsedTimer elapsed; elapsed.start();
        QList<QByteArray> actual;
        QStringList errors;
        connect(&server,&QTcpServer::newConnection,this,[&]()
        {
            peer = server.nextPendingConnection();
            connect(peer,&QTcpSocket::readyRead,this,[&]()
            {
                while (peer && peer->bytesAvailable())
                {
                    const auto batch = decoder.feed(peer->read(16384),elapsed.elapsed());
                    errors.append(batch.errors); actual.append(batch.messages);
                }
            });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        DeviceControl controller;
        QVERIFY(controller.open(QStringLiteral("127.0.0.1"),server.serverPort()));
        QTRY_VERIFY(controller.state().connected && peer);
        QTRY_VERIFY(actual.contains(QByteArray("Qj")));
        const QByteArray prefix("Tt[S01][1][");
        const QByteArray message = prefix + QByteArray(128000 - prefix.size() - 1,'A') + ']';
        QCOMPARE(message.size(),128000);
        QVERIFY(controller.sendTaskReply(message));
        QTRY_VERIFY(actual.contains(message));
        QVERIFY(errors.isEmpty());
        QCOMPARE(actual.count(message),1);
        qInfo() << "RK_TASK_TCP_COMPLETE bytes=" << message.size() << "decodedCount=" << actual.count(message)
                << "elapsed_ms=" << elapsed.elapsed();
        controller.close();
    }
};
QTEST_GUILESS_MAIN(OriginalTaskTransportRkTest)
#include "original_task_transport_rk_test.moc"
