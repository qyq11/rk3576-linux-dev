#ifndef HOSPITAL_DICTIONARY_QUERY_H
#define HOSPITAL_DICTIONARY_QUERY_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QUuid>

// 只限制查询条件：沿原1个头字符+32个尾字符的容量。
// 与QString::size()一致按UTF-16代码单元计数，不限制字典名称或患者字段。
constexpr int kHospitalDictionaryFilterLimit = 33;

// 建议共享合同；由根窗口决定 HospitalStore 正式 API，未写入 06。
struct HospitalDictionaryRequest
{
    QUuid requestId;
    QString type;
    QString filter;
};

struct HospitalDictionaryEntry
{
    qint64 id = 0;
    QString name;
    QString mnemonic;
};
using HospitalDictionaryEntries = QList<HospitalDictionaryEntry>;

struct HospitalDictionaryReply
{
    HospitalDictionaryRequest request;
    bool succeeded = false;
    HospitalDictionaryEntries entries;
    QString error;
};

Q_DECLARE_METATYPE(HospitalDictionaryRequest)
Q_DECLARE_METATYPE(HospitalDictionaryEntry)
Q_DECLARE_METATYPE(HospitalDictionaryEntries)
Q_DECLARE_METATYPE(HospitalDictionaryReply)

inline void registerHospitalDictionaryTypes()
{
    qRegisterMetaType<HospitalDictionaryRequest>("HospitalDictionaryRequest");
    qRegisterMetaType<HospitalDictionaryEntry>("HospitalDictionaryEntry");
    qRegisterMetaType<HospitalDictionaryEntries>("HospitalDictionaryEntries");
    qRegisterMetaType<HospitalDictionaryReply>("HospitalDictionaryReply");
}

inline bool sameHospitalDictionaryRequest(const HospitalDictionaryRequest &left,
                                          const HospitalDictionaryRequest &right)
{
    return !left.requestId.isNull() && left.requestId == right.requestId
            && left.type == right.type && left.filter == right.filter;
}

#endif
