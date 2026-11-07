/****************************************************************************
** Meta object code from reading C++ file 'frmhospinfosetting.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../compile_inputs/hospital/06_programs/sn100_host/ui/frmhospinfosetting.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'frmhospinfosetting.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_frmHospInfoSetting_t {
    QByteArrayData data[21];
    char stringdata0[258];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_frmHospInfoSetting_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_frmHospInfoSetting_t qt_meta_stringdata_frmHospInfoSetting = {
    {
QT_MOC_LITERAL(0, 0, 18), // "frmHospInfoSetting"
QT_MOC_LITERAL(1, 19, 11), // "requestLoad"
QT_MOC_LITERAL(2, 31, 0), // ""
QT_MOC_LITERAL(3, 32, 4), // "type"
QT_MOC_LITERAL(4, 37, 11), // "requestSave"
QT_MOC_LITERAL(5, 49, 12), // "hospitalName"
QT_MOC_LITERAL(6, 62, 12), // "HospitalRows"
QT_MOC_LITERAL(7, 75, 4), // "rows"
QT_MOC_LITERAL(8, 80, 13), // "requestRemove"
QT_MOC_LITERAL(9, 94, 2), // "id"
QT_MOC_LITERAL(10, 97, 17), // "operationFinished"
QT_MOC_LITERAL(11, 115, 9), // "operation"
QT_MOC_LITERAL(12, 125, 7), // "success"
QT_MOC_LITERAL(13, 133, 13), // "statusChanged"
QT_MOC_LITERAL(14, 147, 7), // "message"
QT_MOC_LITERAL(15, 155, 5), // "error"
QT_MOC_LITERAL(16, 161, 17), // "on_btnAdd_clicked"
QT_MOC_LITERAL(17, 179, 17), // "on_btnDel_clicked"
QT_MOC_LITERAL(18, 197, 18), // "on_btnSave_clicked"
QT_MOC_LITERAL(19, 216, 20), // "on_btnCancel_clicked"
QT_MOC_LITERAL(20, 237, 20) // "on_btnReload_clicked"

    },
    "frmHospInfoSetting\0requestLoad\0\0type\0"
    "requestSave\0hospitalName\0HospitalRows\0"
    "rows\0requestRemove\0id\0operationFinished\0"
    "operation\0success\0statusChanged\0message\0"
    "error\0on_btnAdd_clicked\0on_btnDel_clicked\0"
    "on_btnSave_clicked\0on_btnCancel_clicked\0"
    "on_btnReload_clicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_frmHospInfoSetting[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   64,    2, 0x06 /* Public */,
       4,    3,   67,    2, 0x06 /* Public */,
       8,    2,   74,    2, 0x06 /* Public */,
      10,    2,   79,    2, 0x06 /* Public */,
      13,    2,   84,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      16,    0,   89,    2, 0x08 /* Private */,
      17,    0,   90,    2, 0x08 /* Private */,
      18,    0,   91,    2, 0x08 /* Private */,
      19,    0,   92,    2, 0x08 /* Private */,
      20,    0,   93,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, 0x80000000 | 6,    3,    5,    7,
    QMetaType::Void, QMetaType::QString, QMetaType::LongLong,    3,    9,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   11,   12,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   14,   15,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void frmHospInfoSetting::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<frmHospInfoSetting *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->requestLoad((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->requestSave((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< HospitalRows(*)>(_a[3]))); break;
        case 2: _t->requestRemove((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< qint64(*)>(_a[2]))); break;
        case 3: _t->operationFinished((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 4: _t->statusChanged((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 5: _t->on_btnAdd_clicked(); break;
        case 6: _t->on_btnDel_clicked(); break;
        case 7: _t->on_btnSave_clicked(); break;
        case 8: _t->on_btnCancel_clicked(); break;
        case 9: _t->on_btnReload_clicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 2:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< HospitalRows >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (frmHospInfoSetting::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&frmHospInfoSetting::requestLoad)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (frmHospInfoSetting::*)(QString , QString , HospitalRows );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&frmHospInfoSetting::requestSave)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (frmHospInfoSetting::*)(QString , qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&frmHospInfoSetting::requestRemove)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (frmHospInfoSetting::*)(QString , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&frmHospInfoSetting::operationFinished)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (frmHospInfoSetting::*)(QString , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&frmHospInfoSetting::statusChanged)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject frmHospInfoSetting::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_frmHospInfoSetting.data,
    qt_meta_data_frmHospInfoSetting,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *frmHospInfoSetting::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *frmHospInfoSetting::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_frmHospInfoSetting.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int frmHospInfoSetting::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void frmHospInfoSetting::requestLoad(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void frmHospInfoSetting::requestSave(QString _t1, QString _t2, HospitalRows _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void frmHospInfoSetting::requestRemove(QString _t1, qint64 _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void frmHospInfoSetting::operationFinished(QString _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void frmHospInfoSetting::statusChanged(QString _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
