/****************************************************************************
** Meta object code from reading C++ file 'maintenance_store.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../06_programs/sn100_host/instrument/maintenance_store.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'maintenance_store.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MaintenanceStore_t {
    QByteArrayData data[39];
    char stringdata0[464];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MaintenanceStore_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MaintenanceStore_t qt_meta_stringdata_MaintenanceStore = {
    {
QT_MOC_LITERAL(0, 0, 16), // "MaintenanceStore"
QT_MOC_LITERAL(1, 17, 5), // "ready"
QT_MOC_LITERAL(2, 23, 0), // ""
QT_MOC_LITERAL(3, 24, 6), // "loaded"
QT_MOC_LITERAL(4, 31, 18), // "MaintenanceRecords"
QT_MOC_LITERAL(5, 50, 7), // "records"
QT_MOC_LITERAL(6, 58, 9), // "succeeded"
QT_MOC_LITERAL(7, 68, 6), // "action"
QT_MOC_LITERAL(8, 75, 5), // "error"
QT_MOC_LITERAL(9, 81, 9), // "operation"
QT_MOC_LITERAL(10, 91, 7), // "message"
QT_MOC_LITERAL(11, 99, 6), // "closed"
QT_MOC_LITERAL(12, 106, 23), // "connectionSettingsReady"
QT_MOC_LITERAL(13, 130, 9), // "requestId"
QT_MOC_LITERAL(14, 140, 7), // "address"
QT_MOC_LITERAL(15, 148, 4), // "port"
QT_MOC_LITERAL(16, 153, 11), // "fingerprint"
QT_MOC_LITERAL(17, 165, 5), // "saved"
QT_MOC_LITERAL(18, 171, 23), // "connectionSettingsError"
QT_MOC_LITERAL(19, 195, 14), // "requiresReload"
QT_MOC_LITERAL(20, 210, 26), // "connectionAlarmPolicyReady"
QT_MOC_LITERAL(21, 237, 13), // "blockingCodes"
QT_MOC_LITERAL(22, 251, 9), // "available"
QT_MOC_LITERAL(23, 261, 10), // "initialize"
QT_MOC_LITERAL(24, 272, 15), // "absoluteDataDir"
QT_MOC_LITERAL(25, 288, 18), // "initializeOriginal"
QT_MOC_LITERAL(26, 307, 4), // "root"
QT_MOC_LITERAL(27, 312, 10), // "setSession"
QT_MOC_LITERAL(28, 323, 14), // "AccountSession"
QT_MOC_LITERAL(29, 338, 7), // "session"
QT_MOC_LITERAL(30, 346, 9), // "loadMonth"
QT_MOC_LITERAL(31, 356, 5), // "month"
QT_MOC_LITERAL(32, 362, 6), // "record"
QT_MOC_LITERAL(33, 369, 4), // "date"
QT_MOC_LITERAL(34, 374, 22), // "loadConnectionSettings"
QT_MOC_LITERAL(35, 397, 17), // "configurationFile"
QT_MOC_LITERAL(36, 415, 22), // "saveConnectionSettings"
QT_MOC_LITERAL(37, 438, 19), // "expectedFingerprint"
QT_MOC_LITERAL(38, 458, 5) // "close"

    },
    "MaintenanceStore\0ready\0\0loaded\0"
    "MaintenanceRecords\0records\0succeeded\0"
    "action\0error\0operation\0message\0closed\0"
    "connectionSettingsReady\0requestId\0"
    "address\0port\0fingerprint\0saved\0"
    "connectionSettingsError\0requiresReload\0"
    "connectionAlarmPolicyReady\0blockingCodes\0"
    "available\0initialize\0absoluteDataDir\0"
    "initializeOriginal\0root\0setSession\0"
    "AccountSession\0session\0loadMonth\0month\0"
    "record\0date\0loadConnectionSettings\0"
    "configurationFile\0saveConnectionSettings\0"
    "expectedFingerprint\0close"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MaintenanceStore[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      16,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       8,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   94,    2, 0x06 /* Public */,
       3,    1,   95,    2, 0x06 /* Public */,
       6,    1,   98,    2, 0x06 /* Public */,
       8,    2,  101,    2, 0x06 /* Public */,
      11,    0,  106,    2, 0x06 /* Public */,
      12,    5,  107,    2, 0x06 /* Public */,
      18,    3,  118,    2, 0x06 /* Public */,
      20,    3,  125,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      23,    1,  132,    2, 0x0a /* Public */,
      25,    1,  135,    2, 0x0a /* Public */,
      27,    1,  138,    2, 0x0a /* Public */,
      30,    1,  141,    2, 0x0a /* Public */,
      32,    2,  144,    2, 0x0a /* Public */,
      34,    2,  149,    2, 0x0a /* Public */,
      36,    5,  154,    2, 0x0a /* Public */,
      38,    0,  165,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    9,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::Bool,   13,   14,   15,   16,   17,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString, QMetaType::Bool,   13,   10,   19,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QStringList, QMetaType::Bool,   13,   21,   22,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,   24,
    QMetaType::Void, QMetaType::QString,   26,
    QMetaType::Void, 0x80000000 | 28,   29,
    QMetaType::Void, QMetaType::QDate,   31,
    QMetaType::Void, QMetaType::QString, QMetaType::QDate,    7,   33,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString,   13,   35,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::QString,   13,   35,   37,   14,   15,
    QMetaType::Void,

       0        // eod
};

void MaintenanceStore::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MaintenanceStore *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->ready(); break;
        case 1: _t->loaded((*reinterpret_cast< MaintenanceRecords(*)>(_a[1]))); break;
        case 2: _t->succeeded((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 3: _t->error((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 4: _t->closed(); break;
        case 5: _t->connectionSettingsReady((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< QString(*)>(_a[3])),(*reinterpret_cast< QString(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5]))); break;
        case 6: _t->connectionSettingsError((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 7: _t->connectionAlarmPolicyReady((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< QStringList(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 8: _t->initialize((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 9: _t->initializeOriginal((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 10: _t->setSession((*reinterpret_cast< AccountSession(*)>(_a[1]))); break;
        case 11: _t->loadMonth((*reinterpret_cast< QDate(*)>(_a[1]))); break;
        case 12: _t->record((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QDate(*)>(_a[2]))); break;
        case 13: _t->loadConnectionSettings((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 14: _t->saveConnectionSettings((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< QString(*)>(_a[3])),(*reinterpret_cast< QString(*)>(_a[4])),(*reinterpret_cast< QString(*)>(_a[5]))); break;
        case 15: _t->close(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< MaintenanceRecords >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< AccountSession >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MaintenanceStore::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::ready)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(MaintenanceRecords );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::loaded)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::succeeded)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(QString , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::error)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::closed)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(quint64 , QString , QString , QString , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::connectionSettingsReady)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(quint64 , QString , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::connectionSettingsError)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (MaintenanceStore::*)(quint64 , QStringList , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MaintenanceStore::connectionAlarmPolicyReady)) {
                *result = 7;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MaintenanceStore::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_MaintenanceStore.data,
    qt_meta_data_MaintenanceStore,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MaintenanceStore::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MaintenanceStore::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MaintenanceStore.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MaintenanceStore::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    }
    return _id;
}

// SIGNAL 0
void MaintenanceStore::ready()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MaintenanceStore::loaded(MaintenanceRecords _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void MaintenanceStore::succeeded(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void MaintenanceStore::error(QString _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void MaintenanceStore::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void MaintenanceStore::connectionSettingsReady(quint64 _t1, QString _t2, QString _t3, QString _t4, bool _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void MaintenanceStore::connectionSettingsError(quint64 _t1, QString _t2, bool _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void MaintenanceStore::connectionAlarmPolicyReady(quint64 _t1, QStringList _t2, bool _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
