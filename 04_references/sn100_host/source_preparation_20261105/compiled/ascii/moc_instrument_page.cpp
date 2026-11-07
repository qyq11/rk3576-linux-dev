/****************************************************************************
** Meta object code from reading C++ file 'instrument_page.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../compile_inputs/ascii/06_programs/sn100_host/instrument/instrument_page.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'instrument_page.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_InstrumentPage_t {
    QByteArrayData data[18];
    char stringdata0[213];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_InstrumentPage_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_InstrumentPage_t qt_meta_stringdata_InstrumentPage = {
    {
QT_MOC_LITERAL(0, 0, 14), // "InstrumentPage"
QT_MOC_LITERAL(1, 15, 13), // "statusChanged"
QT_MOC_LITERAL(2, 29, 0), // ""
QT_MOC_LITERAL(3, 30, 7), // "message"
QT_MOC_LITERAL(4, 38, 11), // "busyChanged"
QT_MOC_LITERAL(5, 50, 4), // "busy"
QT_MOC_LITERAL(6, 55, 17), // "operationFinished"
QT_MOC_LITERAL(7, 73, 9), // "operation"
QT_MOC_LITERAL(8, 83, 7), // "success"
QT_MOC_LITERAL(9, 91, 28), // "manualCommunicationRequested"
QT_MOC_LITERAL(10, 120, 7), // "request"
QT_MOC_LITERAL(11, 128, 7), // "session"
QT_MOC_LITERAL(12, 136, 10), // "connection"
QT_MOC_LITERAL(13, 147, 14), // "AccountSession"
QT_MOC_LITERAL(14, 162, 8), // "identity"
QT_MOC_LITERAL(15, 171, 7), // "payload"
QT_MOC_LITERAL(16, 179, 26), // "manualCommunicationRevoked"
QT_MOC_LITERAL(17, 206, 6) // "reason"

    },
    "InstrumentPage\0statusChanged\0\0message\0"
    "busyChanged\0busy\0operationFinished\0"
    "operation\0success\0manualCommunicationRequested\0"
    "request\0session\0connection\0AccountSession\0"
    "identity\0payload\0manualCommunicationRevoked\0"
    "reason"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_InstrumentPage[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   39,    2, 0x06 /* Public */,
       4,    1,   42,    2, 0x06 /* Public */,
       6,    2,   45,    2, 0x06 /* Public */,
       9,    5,   50,    2, 0x06 /* Public */,
      16,    4,   61,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    7,    8,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::ULongLong, 0x80000000 | 13, QMetaType::QByteArray,   10,   11,   12,   14,   15,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QString,   10,   11,   12,   17,

       0        // eod
};

void InstrumentPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<InstrumentPage *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->statusChanged((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->busyChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->operationFinished((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 3: _t->manualCommunicationRequested((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< AccountSession(*)>(_a[4])),(*reinterpret_cast< QByteArray(*)>(_a[5]))); break;
        case 4: _t->manualCommunicationRevoked((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< QString(*)>(_a[4]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 3:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< AccountSession >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (InstrumentPage::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&InstrumentPage::statusChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (InstrumentPage::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&InstrumentPage::busyChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (InstrumentPage::*)(QString , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&InstrumentPage::operationFinished)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (InstrumentPage::*)(quint64 , quint64 , quint64 , AccountSession , QByteArray );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&InstrumentPage::manualCommunicationRequested)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (InstrumentPage::*)(quint64 , quint64 , quint64 , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&InstrumentPage::manualCommunicationRevoked)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject InstrumentPage::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_InstrumentPage.data,
    qt_meta_data_InstrumentPage,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *InstrumentPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *InstrumentPage::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_InstrumentPage.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int InstrumentPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void InstrumentPage::statusChanged(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void InstrumentPage::busyChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void InstrumentPage::operationFinished(QString _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void InstrumentPage::manualCommunicationRequested(quint64 _t1, quint64 _t2, quint64 _t3, AccountSession _t4, QByteArray _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void InstrumentPage::manualCommunicationRevoked(quint64 _t1, quint64 _t2, quint64 _t3, QString _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
