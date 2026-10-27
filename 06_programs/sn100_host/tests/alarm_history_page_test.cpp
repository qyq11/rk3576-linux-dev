// Prepared anonymous real-worker GUI tests only. Not compiled or executed yet.
#include "alarm_history_host.h"
#include "calendar/cuscalendar.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include <QtTest>
#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSemaphore>
#include <QSharedPointer>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <functional>

namespace
{
// Formal tests keep runtime data under this repository's temp/sn100_host.
// The caller must create a fresh empty dedicated directory before invocation.
// Resolve every existing ancestor, rejecting links/junction aliases and escapes.
QString anonymousTestRootError(const QString &suppliedRoot, QString *canonicalRoot)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    const QString sourceFile = QFileInfo(QString::fromUtf8(__FILE__)).canonicalFilePath();
    if (sourceFile.isEmpty())
    {
        return QStringLiteral("无法从正式测试源码定位项目根；未创建匿名库。");
    }
    QDir repository(QFileInfo(sourceFile).absolutePath());
    if (repository.dirName() != QStringLiteral("tests") || !repository.cdUp() ||
        repository.dirName() != QStringLiteral("sn100_host") || !repository.cdUp() ||
        repository.dirName() != QStringLiteral("06_programs") || !repository.cdUp())
    {
        return QStringLiteral("测试源码不在本项目06_programs/sn100_host/tests，拒绝猜测输出位置。");
    }
    const QString repositoryRoot = repository.canonicalPath();
    const QString base = QDir::cleanPath(QDir(repositoryRoot).filePath(QStringLiteral("temp/sn100_host")));
    if (repositoryRoot.isEmpty() || suppliedRoot.isEmpty() || suppliedRoot.contains(QChar::Null) ||
        !QFileInfo(suppliedRoot).isAbsolute())
    {
        return QStringLiteral("须显式指定本项目内的匿名测试目录绝对路径。");
    }
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(suppliedRoot));
    const QString resolvedRoot = QFileInfo(root).canonicalFilePath();
    if (!root.startsWith(base + QLatin1Char('/'), pathCase) || resolvedRoot.isEmpty() ||
        !resolvedRoot.startsWith(base + QLatin1Char('/'), pathCase))
    {
        return QStringLiteral("匿名目录必须是本项目temp/sn100_host内已存在的专用子目录。");
    }
    QString ancestor = root;
    while (ancestor.compare(repositoryRoot, pathCase) != 0)
    {
        const QFileInfo info(ancestor);
        const QString resolvedAncestor = info.canonicalFilePath();
        if (!info.exists() || !info.isDir() || info.isSymLink() || resolvedAncestor.isEmpty() ||
            QDir::cleanPath(resolvedAncestor).compare(ancestor, pathCase) != 0)
        {
            return QStringLiteral("匿名目录或现有祖先含链接、路径别名或不是目录，拒绝路径逃逸。");
        }
        const QString parent = QDir::cleanPath(info.absolutePath());
        if (parent == ancestor)
        {
            return QStringLiteral("匿名目录祖先未回到本项目根，拒绝继续。");
        }
        ancestor = parent;
    }
    const QDir directory(resolvedRoot);
    const QFileInfo directoryInfo(resolvedRoot);
    if (!directoryInfo.isReadable() || !directoryInfo.isWritable())
    {
        return QStringLiteral("匿名专用目录须可读取和写入，无法检查已有内容时拒绝继续。");
    }
    if (!directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                             QDir::NoDotAndDotDot).isEmpty())
    {
        return QStringLiteral("匿名专用目录必须为空；发现已有文件、子目录或链接，未覆盖或清理。");
    }
    *canonicalRoot = resolvedRoot;
    return QString();
}

bool until(const std::function<bool()> &predicate)
{QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<10000)QTest::qWait(5);return predicate();}
template<class T>T *find(QObject *parent,const char *name)
{return parent?parent->findChild<T*>(QString::fromLatin1(name)):nullptr;}
bool ready(QDialog *dialog)
{auto *mask=find<CusDialogMask>(dialog,"mask");return mask&&until([&](){return mask->isReadyForInput();});}
bool click(QDialog *dialog,const char *name)
{auto *button=find<QPushButton>(dialog,name);if(!button||!ready(dialog)||!button->isVisible()||!button->isEnabled())return false;
    QTest::mouseClick(button,Qt::LeftButton);return true;}
bool makeHistory(QString path)
{
    const QString connection=QUuid::createUuid().toString();bool ok=false;
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(path);
        if(db.open())
        {
            QSqlQuery query(db);ok=query.exec("CREATE TABLE abnormal_log(id INTEGER PRIMARY KEY AUTOINCREMENT,error_code VARCHAR,error_describe VARCHAR,measure VARCHAR,date_time DATETIME,corrective_action VARCHAR,use_user VARCHAR)");
            for(int id=1;ok&&id<=26;++id)
            {
                QString date;
                if(id==1)date="2026/10/26 00:00:00";else if(id==2)date="2026/10/26 23:59:59";
                else if(id==3||id==4)date="2026/10/26 12:00:00";
                else if(id<24)date=QString("2026/10/26 10:00:%1").arg(id,2,10,QChar('0'));
                else if(id==24)date="2026/10/25 00:00:00";else if(id==25)date="2026/10/25 23:59:59";else date="2026/10/27 00:00:00";
                query.prepare("INSERT INTO abnormal_log VALUES(?,?,?,?,?,?,?)");
                const QVariantList values={id,id==3||id==4?QStringLiteral("E*重复"):QString("ANON%1").arg(id),
                    QStringLiteral("匿名描述%1").arg(id),QStringLiteral("匿名措施"),date,QVariant(),QStringLiteral("匿名操作者")};
                for(const auto &value:values)query.addBindValue(value);ok=query.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);return ok;
}
QByteArray sourceImage(QString path,bool *ok)
{
    const QString connection=QUuid::createUuid().toString();QByteArray bytes;*ok=false;
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if(db.open())
        {
            QSqlQuery query(db);QDataStream stream(&bytes,QIODevice::WriteOnly);
            if(query.exec("SELECT id,error_code,error_describe,measure,date_time,corrective_action,use_user FROM abnormal_log ORDER BY id"))
            {
                while(query.next())for(int column=0;column<7;++column)stream<<query.value(column);
                *ok=!query.lastError().isValid()&&stream.status()==QDataStream::Ok;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);return bytes;
}
class Fixture
{
public:
    explicit Fixture(QString root):directory(QDir(root).filePath("page-XXXXXX")),store(new AlarmHistory::Store)
    {
        directory.setAutoRemove(false);AlarmHistory::Store::registerTypes();store->moveToThread(&thread);
        QObject::connect(&thread,&QThread::finished,store,&QObject::deleteLater);
        QObject::connect(store,&AlarmHistory::Store::closed,&context,[this](){closed=true;},Qt::QueuedConnection);
        owner.resize(1600,900);owner.show();host=new AlarmHistoryHost(store,&owner,&context);thread.start();
    }
    ~Fixture(){if(!stop())qFatal("Anonymous history page worker did not stop");}
    bool seed()
    {path=directory.filePath("anonymous_history.db");if(!directory.isValid()||!makeHistory(path))return false;
        bool ok=false;before=sourceImage(path,&ok);return seeded=ok;}
    bool open()
    {return host->showExistingHistory(path,QDate(2026,10,26))&&until([&](){return host->page()&&host->page()->isComplete()&&!host->page()->isBusy();});}
    bool acknowledge()
    {
        auto *warning=find<CusMessageBox>(host->page(),"AlarmHistoryWarning");if(!warning)return true;
        if(!until([&](){return warning->isReadyForInput();}))return false;
        QPointer<CusMessageBox> guard(warning);auto *sure=find<QPushButton>(warning,"btnSure");if(!sure)return false;
        QTest::mouseClick(sure,Qt::LeftButton);return until([&](){return guard.isNull();});
    }
    bool stop()
    {
        if(stopped)return true;
        bool normalPageClose=true;
        if(host->page())
        {
            acknowledge();host->page()->requestClose();
            if(!until([&](){return !host->page();}))
            {
                // Failure cleanup is distinct from the normal-close assertions.
                normalPageClose=false;
                QMetaObject::invokeMethod(store,"close",Qt::QueuedConnection);until([&](){return closed;});
                delete host->page();
            }
        }
        if(!closed)QMetaObject::invokeMethod(store,"close",Qt::QueuedConnection);
        const bool actualClosed=until([&](){return closed;});thread.quit();const bool joined=thread.wait(5000);stopped=joined;
        bool sqlUnchanged=true;if(seeded){bool ok=false;const auto after=sourceImage(path,&ok);sqlUnchanged=ok&&after==before;}
        qInfo()<<"ALARM_HISTORY_PAGE_STORE_CLOSED"<<actualClosed<<"WORKER_JOINED"<<joined<<"SQL_ROW_IMAGE_UNCHANGED"<<sqlUnchanged
               <<"NORMAL_PAGE_CLOSE"<<normalPageClose;
        return actualClosed&&joined&&sqlUnchanged&&normalPageClose;
    }
    QObject context;QTemporaryDir directory;QThread thread;AlarmHistory::Store *store;
    QWidget owner;AlarmHistoryHost *host;QString path;QByteArray before;bool closed=false,stopped=false,seeded=false;
};
QList<qint64> ids(QTableWidget *table)
{QList<qint64> result;for(int row=0;row<table->rowCount();++row)result.append(table->item(row,0)->data(Qt::UserRole).toLongLong());return result;}
bool chooseSameMonth(QDialog *page,bool first,QDate date)
{
    if(!click(page,first?"btnCalendarSetStart":"btnCalendarSetEnd"))return false;
    auto *popup=find<QDialog>(page,"AlarmHistoryCalendar");if(!popup||!ready(popup))return false;
    auto *calendar=find<CusCalendar>(popup,"widget");if(!calendar)return false;
    // Original calendar's documented fixed450x376 grid, header offset113;
    // only fixed October2026 dates are used, not a fabricated selection signal.
    const int slot=QDate(date.year(),date.month(),1).dayOfWeek()-1+date.day()-1;
    const double width=450.0/7,height=(376.0-113)/7;
    QPointer<QDialog> guard(popup);
    QTest::mouseClick(calendar,Qt::LeftButton,Qt::NoModifier,QPoint(int((slot%7+.5)*width),int(113+(slot/7+1.5)*height)));
    return until([&](){return guard.isNull();})&&find<QLineEdit>(page,first?"ldtStartDate":"ldtEndDate")->text()==date.toString("yyyy/MM/dd");
}
}

class AlarmHistoryPageTest : public QObject
{
    Q_OBJECT
    QString root_;
private slots:
    void initTestCase()
    {
        QApplication::setStyle("Fusion");QApplication::setFont(QFont("Noto Sans CJK SC",11));
        const QString rootError=anonymousTestRootError(qEnvironmentVariable("SN100_ALARM_PAGE_TEST_ROOT"),&root_);
        QVERIFY2(rootError.isEmpty(),qPrintable(rootError));
    }
    void originalGeometryRecentTwenty()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());
        QVERIFY(fixture.host->showExistingHistory(fixture.path,QDate(2026,10,26)));
        auto *page=fixture.host->page();QVERIFY(page);
        struct Counts{int slices=0,pings=0,maxSlice=0;};auto counts=QSharedPointer<Counts>::create();
        connect(page,&AlarmHistoryPage::appendTurnApplied,page,[page,counts](int count)
        {++counts->slices;counts->maxSlice=qMax(counts->maxSlice,count);QMetaObject::invokeMethod(page,[counts](){++counts->pings;},Qt::QueuedConnection);});
        QVERIFY(until([&](){return page->isComplete()&&!page->isBusy();}));
        auto *table=find<QTableWidget>(page,"tlbAbnormallog");QVERIFY(table);QCOMPARE(table->columnCount(),4);
        QCOMPARE(table->geometry(),QRect(14,0,961,751));QCOMPARE(find<QWidget>(page,"view_frame")->size(),QSize(995,878));
        QCOMPARE(find<QLabel>(page,"alarmHistoryStatus")->geometry(),QRect(14,1,925,28));
        QCOMPARE(find<QStackedWidget>(page,"stackedWidget")->currentIndex(),1);
        for(const char *name:{"btnReturn","btnReset","btnErrorLog","tblAbnormal"})QVERIFY(!find<QWidget>(page,name)->isEnabled());
        QCOMPARE(ids(table),QList<qint64>({2,4,3,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7}));
        QVERIFY(page->statusText().contains(QStringLiteral("最近20")));QVERIFY(page->statusText().contains("23"));
        QVERIFY(counts->maxSlice<=32);QVERIFY(counts->slices>=1);QVERIFY(counts->pings>=counts->slices-1);QVERIFY(fixture.stop());
    }
    void calendarDraftThenFullIntervalLastBatchBeforeCompletion()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        AlarmHistoryPage::DisplayBudget budget;budget.rowsPerTurn=4;QVERIFY(page->setDisplayBudget(budget));
        struct Turns{int count=0,pings=0;bool missedYield=false;};auto turns=QSharedPointer<Turns>::create();
        connect(page,&AlarmHistoryPage::appendTurnApplied,page,[page,turns](int count)
        {if(turns->count>0&&turns->pings<turns->count)turns->missedYield=true;
            ++turns->count;QVERIFY(count<=4);QMetaObject::invokeMethod(page,[turns](){++turns->pings;},Qt::QueuedConnection);});
        QVERIFY(chooseSameMonth(page,true,QDate(2026,10,25)));QVERIFY(!page->isComplete());
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);
        QVERIFY(click(page,"btnQuery"));QVERIFY(until([&](){return page->isComplete()&&!page->isBusy();}));
        QCOMPARE(ids(find<QTableWidget>(page,"tlbAbnormallog")),QList<qint64>({2,4,3,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,1,25,24}));
        QCOMPARE(turns->count,7);QVERIFY(!turns->missedYield);
        QVERIFY(page->statusText().contains(QStringLiteral("区间完整")));QVERIFY(fixture.stop());
    }
    void finalBatchCapacityCancellationNeverCompletesPartial()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        AlarmHistoryPage::DisplayBudget budget;budget.rows=10;QVERIFY(page->setDisplayBudget(budget));
        QVERIFY(click(page,"btnQuery"));
        QVERIFY(until([&](){return page->statusText().contains(QStringLiteral("超过显示候选"));}));
        QVERIFY(fixture.acknowledge());QVERIFY(until([&](){return !page->isBusy();}));QVERIFY(!page->isComplete());
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);QVERIFY(fixture.stop());
    }
    void utf8TextCapacityClearsInsteadOfPartialSuccess()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        AlarmHistoryPage::DisplayBudget budget;budget.textBytes=100;QVERIFY(page->setDisplayBudget(budget));
        QVERIFY(click(page,"btnQuery"));
        QVERIFY(until([&](){return page->statusText().contains(QStringLiteral("超过显示候选"));}));
        QVERIFY(fixture.acknowledge());QVERIFY(until([&](){return !page->isBusy();}));QVERIFY(!page->isComplete());
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);QVERIFY(fixture.stop());
    }
    void cancelButtonAfterRealSqlTerminalReleasesGuiAppend()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        AlarmHistoryPage::DisplayBudget budget;budget.rowsPerTurn=1;QVERIFY(page->setDisplayBudget(budget));
        auto terminal=QSharedPointer<QSemaphore>::create();
        struct State{int advances=0;bool ordered=false,clicked=false,completeAtReceipt=false;};auto state=QSharedPointer<State>::create();
        connect(page,&AlarmHistoryPage::requestAdvance,page,[state,terminal](AlarmHistory::Key,quint64)
        {
            // Fixed26-row input: validation credit then final reading credit.
            // Queue actual worker advance first, then hold GUI before delivery
            // until that real SQL terminal exists. This is ordering, not timing.
            if(++state->advances==2)state->ordered=terminal->tryAcquire(1,3000);
        });
        const QPointer<AlarmHistoryPage> guard(page);
        connect(fixture.store,&AlarmHistory::Store::finished,&fixture.context,
            [guard,state](AlarmHistory::Request request,AlarmHistory::Completion,qint64,qint64)
            {
                if(request.scope!=AlarmHistory::Scope::DateInterval||!guard)return;
                state->completeAtReceipt=guard->isComplete();
                auto *button=find<QPushButton>(guard,"btnQuery");
                if(button&&button->isVisible()&&button->isEnabled()&&guard->isBusy())
                {state->clicked=true;QTest::mouseClick(button,Qt::LeftButton);}
            },Qt::QueuedConnection);
        // Register last: both actual terminal deliveries are queued before the
        // GUI's ordering barrier is released. Only QSemaphore is touched here.
        const auto receipt=connect(fixture.store,&AlarmHistory::Store::finished,&fixture.context,
            [terminal](AlarmHistory::Request request,AlarmHistory::Completion,qint64,qint64)
            {if(request.scope==AlarmHistory::Scope::DateInterval)terminal->release();},Qt::DirectConnection);
        QVERIFY(click(page,"btnQuery"));QVERIFY(until([&](){return state->clicked&&!page->isBusy();}));
        QVERIFY(state->ordered);QVERIFY(!state->completeAtReceipt);QVERIFY(!page->isComplete());
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);
        disconnect(receipt);QVERIFY(fixture.stop());
    }
    void calendarEscapeKeepsResultAndLegalEmptyIsComplete()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        const auto before=ids(find<QTableWidget>(page,"tlbAbnormallog"));
        QVERIFY(click(page,"btnCalendarSetStart"));QPointer<QDialog> popup=find<QDialog>(page,"AlarmHistoryCalendar");
        QVERIFY(popup);QVERIFY(ready(popup));QTest::keyClick(popup,Qt::Key_Escape);QVERIFY(until([&](){return popup.isNull();}));
        QCOMPARE(find<QLineEdit>(page,"ldtStartDate")->text(),QString("2026/10/26"));
        QCOMPARE(ids(find<QTableWidget>(page,"tlbAbnormallog")),before);QVERIFY(page->isComplete());
        QVERIFY(chooseSameMonth(page,true,QDate(2026,10,24)));QVERIFY(chooseSameMonth(page,false,QDate(2026,10,24)));
        QVERIFY(click(page,"btnQuery"));QVERIFY(until([&](){return page->isComplete()&&!page->isBusy();}));
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);QVERIFY(page->statusText().contains(QStringLiteral("完整读取：0")));
        QVERIFY(fixture.stop());
    }
    void missingExistingSourceDoesNotCreateOrFakeEmpty()
    {
        Fixture fixture(root_);fixture.path=fixture.directory.filePath("missing.db");QVERIFY(!QFile::exists(fixture.path));
        QVERIFY(fixture.host->showExistingHistory(fixture.path,QDate(2026,10,26)));
        auto *page=fixture.host->page();QVERIFY(until([&](){return page->statusText().contains(QStringLiteral("打开失败"));}));
        QVERIFY(!page->isComplete());QVERIFY(!QFile::exists(fixture.path));QVERIFY(fixture.acknowledge());QVERIFY(fixture.stop());
    }
    void closeQueuedBeforeWorkerReturnsDropsLateRows()
    {
        Fixture fixture(root_);QVERIFY(fixture.seed());QVERIFY(fixture.open());auto *page=fixture.host->page();
        auto entered=QSharedPointer<QSemaphore>::create(),release=QSharedPointer<QSemaphore>::create();
        struct Release{QSharedPointer<QSemaphore> gate;~Release(){gate->release();}} releaseOnExit{release};
        QMetaObject::invokeMethod(fixture.store,[entered,release](){entered->release();release->tryAcquire(1,3000);},Qt::QueuedConnection);
        QVERIFY(until([&](){return entered->available()>0;}));QVERIFY(click(page,"btnQuery"));QVERIFY(page->isBusy());
        QPointer<AlarmHistoryPage> guard(page);QVERIFY(!page->requestClose());
        QCOMPARE(find<QTableWidget>(page,"tlbAbnormallog")->rowCount(),0);release->release();
        QVERIFY(until([&](){return guard.isNull()&&fixture.closed;}));QVERIFY(fixture.stop());
    }
};
QTEST_MAIN(AlarmHistoryPageTest)
#include "alarm_history_page_test.moc"
