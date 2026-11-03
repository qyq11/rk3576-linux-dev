#include "original_reagent_capacity.h"
#include "original_business_database.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace OriginalRuntime
{
namespace
{
bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}

QString ioError(int code)
{
    return QString::fromLocal8Bit(std::strerror(code));
}

struct ReadDescriptor
{
    explicit ReadDescriptor(int descriptor) : value(descriptor) {}
    ~ReadDescriptor() { if (value>=0) ::close(value); }
    ReadDescriptor(const ReadDescriptor &)=delete;
    ReadDescriptor &operator=(const ReadDescriptor &)=delete;
    int value;
};

bool sameIdentity(const struct stat &before, const struct stat &after)
{
    return before.st_dev==after.st_dev && before.st_ino==after.st_ino &&
        before.st_mode==after.st_mode && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;
}

// 只把系统明确的ENOENT当作缺文件；权限、坏路径和其他读取错误不能变成几何后备。
bool loadOriginalCapacity(const QString &root, ReagentCapacityResult *result,
                          QJsonObject *document, QString *message)
{
    const QString directory=QDir(root).filePath(QStringLiteral("calibrationFile"));
    const QByteArray directoryName=QFile::encodeName(directory);
    const QByteArray rootName=QFile::encodeName(root);
    const QByteArray fileName=QFile::encodeName(result->relativeFile.section('/',-1));
    struct stat rootBefore;
    if (::lstat(rootName.constData(),&rootBefore)!=0)
    {
        const int code=errno;
        return fail(message,QStringLiteral("无法核对原资料工作根：")+ioError(code));
    }
    if (!S_ISDIR(rootBefore.st_mode)) return fail(message,QStringLiteral("原资料工作根不是普通目录。"));
    const auto rootUnchanged=[&]()
    {
        QString problem;
        if (!OriginalBusiness::workingRoot(root,&problem)) return fail(message,problem);
        struct stat current;
        if (::lstat(rootName.constData(),&current)!=0)
        {
            const int code=errno;
            return fail(message,QStringLiteral("无法再次核对原资料工作根：")+ioError(code));
        }
        if (!sameIdentity(rootBefore,current))
            return fail(message,QStringLiteral("读取期间原资料工作根变化，未采用其他目录的容量资料。"));
        return true;
    };
    ReadDescriptor directoryFd(::open(directoryName.constData(),O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK));
    if (directoryFd.value<0)
    {
        const int code=errno;
        if (code!=ENOENT) return fail(message,QStringLiteral("原容量目录打开失败：")+ioError(code));
        struct stat current;
        if (::lstat(directoryName.constData(),&current)==0)
            return fail(message,QStringLiteral("核对期间原容量目录已出现或被替换，请重新读取。"));
        const int checkedCode=errno;
        if (checkedCode!=ENOENT) return fail(message,QStringLiteral("无法确认原容量目录缺失：")+ioError(checkedCode));
        if (!rootUnchanged()) return false;
        result->fileState=ReagentCapacityFileState::Missing;
        return true;
    }
    struct stat directoryBefore;
    if (::fstat(directoryFd.value,&directoryBefore)!=0)
    {
        const int code=errno;
        return fail(message,QStringLiteral("无法核对已打开的容量目录：")+ioError(code));
    }
    if (!S_ISDIR(directoryBefore.st_mode)) return fail(message,QStringLiteral("已打开的原容量路径不是目录。"));
    const auto directoryUnchanged=[&]()
    {
        if (!rootUnchanged()) return false;
        struct stat opened, named;
        if (::fstat(directoryFd.value,&opened)!=0 || ::lstat(directoryName.constData(),&named)!=0)
        {
            const int code=errno;
            return fail(message,QStringLiteral("无法再次核对原容量目录：")+ioError(code));
        }
        if (!sameIdentity(directoryBefore,opened) || !sameIdentity(opened,named))
            return fail(message,QStringLiteral("读取期间原容量目录被替换或修改，未采用旧目录快照。"));
        return true;
    };
    // 目录由已打开的fd固定；即使目标随后变成FIFO，非阻塞打开也不会等写入方。
    ReadDescriptor fileFd(::openat(directoryFd.value,fileName.constData(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if (fileFd.value<0)
    {
        const int code=errno;
        if (code!=ENOENT) return fail(message,QStringLiteral("原容量文件读取失败：")+ioError(code));
        struct stat current;
        if (::fstatat(directoryFd.value,fileName.constData(),&current,AT_SYMLINK_NOFOLLOW)==0)
            return fail(message,QStringLiteral("核对期间原容量文件已出现或被替换，请重新读取。"));
        const int checkedCode=errno;
        if (checkedCode!=ENOENT) return fail(message,QStringLiteral("无法确认原容量文件缺失：")+ioError(checkedCode));
        if (!directoryUnchanged()) return false;
        result->fileState=ReagentCapacityFileState::Missing;
        return true;
    }
    result->fileState=ReagentCapacityFileState::Present;
    struct stat before;
    if (::fstat(fileFd.value,&before)!=0)
    {
        const int code=errno;
        return fail(message,QStringLiteral("无法核对已打开的容量文件：")+ioError(code));
    }
    const qint64 maximumBytes=8*1024*1024;
    if (!S_ISREG(before.st_mode) || before.st_size<0 || before.st_size>maximumBytes)
        return fail(message,QStringLiteral("原容量文件类型不符或超过8MB，未读取半份资料。"));
    QFile file;
    if (!file.open(fileFd.value,QIODevice::ReadOnly,QFileDevice::DontCloseHandle))
        return fail(message,QStringLiteral("无法读取已打开的原容量文件。"));
    const QByteArray bytes=file.read(maximumBytes+1);
    if (file.error()!=QFile::NoError || bytes.size()!=before.st_size || bytes.size()>maximumBytes || !file.atEnd())
        return fail(message,QStringLiteral("原容量文件读取不完整或超过8MB。"));
    struct stat after;
    if (::fstat(fileFd.value,&after)!=0 || !sameIdentity(before,after))
        return fail(message,QStringLiteral("读取期间原容量文件变化，未使用可能混合的资料。"));
    result->fingerprint=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    QByteArray json=bytes;
    if (json.startsWith("\xef\xbb\xbf")) json.remove(0,3);
    // Qt5文档只直接容纳object/array；包装只用于严格识别原版可读的根null，不改原字节。
    QJsonParseError error;
    const auto parsed=QJsonDocument::fromJson('['+json+']',&error);
    if (error.error!=QJsonParseError::NoError || !parsed.isArray() || parsed.array().size()!=1)
        return fail(message,QStringLiteral("原容量文件不是完整有效的JSON；空文件或白文件不算缺文件。"));
    const QJsonValue value=parsed.array().first();
    if (!value.isObject() && !value.isNull())
        return fail(message,QStringLiteral("原容量JSON根不是对象或null，未把错误类型当作缺矩阵。"));
    struct stat named;
    if (::fstatat(directoryFd.value,fileName.constData(),&named,AT_SYMLINK_NOFOLLOW)!=0 ||
        !sameIdentity(after,named))
        return fail(message,QStringLiteral("读取后原容量文件路径已变化，未把旧文件快照当作当前文件。"));
    if (!directoryUnchanged()) return false;
    *document=value.toObject(); // 合法null和空对象按原缺区域路径处理；不要求CONFIG。
    return true;
}

enum class NodeState { Found, Missing, Invalid };
NodeState readNode(const QJsonObject &root, const QStringList &path, QJsonValue *value, QString *reason)
{
    *value=root;
    for (const QString &key : path)
    {
        if (!value->isObject())
        {
            *reason=QStringLiteral("原容量路径的中间节点不是对象：")+path.join('/');
            return NodeState::Invalid;
        }
        const QJsonObject object=value->toObject();
        if (!object.contains(key))
        {
            *reason=QStringLiteral("原容量资料缺键：")+path.join('/');
            return NodeState::Missing;
        }
        *value=object.value(key);
    }
    return NodeState::Found;
}

bool finiteJson(const QJsonValue &value, double *number)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    *number=value.toDouble();
    return true;
}

bool missingMatrix(ReagentCapacityResult *result, const QString &reason)
{
    result->matrixState=ReagentCapacityMatrixState::CoverageMissing;
    result->matrixVolumeMicrolitres=0;
    result->matrixFailureReason=reason;
    return true;
}

bool badMatrix(ReagentCapacityResult *result, QString *message, const QString &reason)
{
    result->matrixState=ReagentCapacityMatrixState::Invalid;
    result->matrixFailureReason=reason;
    return fail(message,reason);
}

bool matrixVolume(const QJsonObject &document, const ReagentCapacityInput &input,
                  const QString &region, const QString &bottle, ReagentCapacityResult *result,
                  QString *message)
{
    QJsonValue area;
    QString reason;
    NodeState state=readNode(document,{region},&area,&reason);
    if (state==NodeState::Missing) return missingMatrix(result,reason);
    if (state==NodeState::Invalid || !area.isObject())
        return badMatrix(result,message,QStringLiteral("原容量区域不是对象。"));
    QString subArea;
    QJsonObject matrix;
    bool found=false;
    const QJsonObject areas=area.toObject();
    for (auto it=areas.begin();it!=areas.end();++it)
    {
        if (!it.value().isObject()) return badMatrix(result,message,QStringLiteral("原容量子区域不是对象。"));
        QJsonValue candidate;
        state=readNode(it.value().toObject(),{"BASE",bottle,input.hole},&candidate,&reason);
        if (state==NodeState::Missing) continue;
        if (state==NodeState::Invalid || !candidate.isObject())
            return badMatrix(result,message,QStringLiteral("原孔位容量矩阵不是对象。"));
        subArea=it.key(); matrix=candidate.toObject(); found=true; break;
    }
    if (!found) return missingMatrix(result,QStringLiteral("此区域、瓶型和孔位没有原BASE矩阵。"));
    QVector<QPair<double,double>> points{{0.0,0.0}};
    for (auto it=matrix.begin();it!=matrix.end();++it)
    {
        bool amountOk=false;
        const double amount=it.key().toDouble(&amountOk);
        double height=0;
        if (!amountOk || !std::isfinite(amount) || !finiteJson(it.value(),&height))
            return badMatrix(result,message,QStringLiteral("原容量矩阵的容量键或高度不是有限数。"));
        points.append({height,amount});
    }
    const QString needle=input.needle==1 ? QStringLiteral("N1") : input.needle==2 ? QStringLiteral("N2") : QStringLiteral("S1");
    QJsonValue offsets;
    state=readNode(document,{region,subArea,needle,bottle},&offsets,&reason);
    if (state==NodeState::Missing) return missingMatrix(result,reason);
    if (state==NodeState::Invalid || !offsets.isObject() || offsets.toObject().isEmpty())
        return badMatrix(result,message,QStringLiteral("原针补偿集合不是非空对象，未解引用空首项。"));
    const QJsonObject offsetHoles=offsets.toObject();
    const auto offsetHole=offsetHoles.begin();
    if (!offsetHole.value().isObject() || offsetHole.value().toObject().isEmpty())
        return badMatrix(result,message,QStringLiteral("原针补偿首孔没有有效容量点。"));
    const QJsonObject marks=offsetHole.value().toObject();
    const auto mark=marks.begin();
    double needleHeight=0, referenceHeight=0;
    if (matrix.contains(mark.key()) && !finiteJson(mark.value(),&needleHeight))
        return badMatrix(result,message,QStringLiteral("原针补偿首点不是有限高度。"));
    QJsonValue reference;
    state=readNode(document,{region,subArea,"BASE",bottle,offsetHole.key(),mark.key()},&reference,&reason);
    if (state==NodeState::Missing) return missingMatrix(result,reason);
    if (state==NodeState::Invalid || !finiteJson(reference,&referenceHeight))
        return badMatrix(result,message,QStringLiteral("原针补偿引用的BASE点不是有限高度。"));
    double height=result->quantizedHeightMm;
    // 原版只有目标孔含补偿容量键才读取针测高；缺这个键时针测高按0参与补偿。
    const double usedNeedleHeight=matrix.contains(mark.key()) ? needleHeight : 0.0;
    if (height>0) height+=referenceHeight-usedNeedleHeight;
    if (!std::isfinite(height)) return badMatrix(result,message,QStringLiteral("原针补偿后的高度不是有限数。"));
    result->adjustedHeightMm=height;
    result->adjustedHeightAvailable=true;
    // 空BASE对象在原版有显式return0；但前面的坏针集合不能因此被吞掉。
    if (points.size()<=1) return missingMatrix(result,QStringLiteral("原孔位BASE为空对象，没有可换算的容量点。"));
    std::stable_sort(points.begin(),points.end(),[](const QPair<double,double> &a,const QPair<double,double> &b)
    { return a.first<b.first; });
    int segment=points.size()-2;
    if (height<points.first().first) segment=0;
    else if (height>points[points.size()-2].first) segment=points.size()-2;
    else
        for (int i=0;i+1<points.size();++i)
            if (points[i].first<=height && height<=points[i+1].first) { segment=i; break; }
    const auto left=points[segment], right=points[segment+1];
    const double slope=(right.second-left.second)/(right.first-left.first+1e-12);
    const double intercept=left.second-slope*left.first;
    double volume=slope*height+intercept;
    if (!std::isfinite(slope) || !std::isfinite(intercept) || !std::isfinite(volume))
        return badMatrix(result,message,QStringLiteral("原分段容量换算不是有限数。"));
    if (volume<=0) volume=0;
    else if (volume>=result->matrixMaximumMicrolitres) volume=result->matrixMaximumMicrolitres;
    result->matrixVolumeMicrolitres=volume;
    result->matrixState=ReagentCapacityMatrixState::Available;
    return true;
}

bool geometryVolume(const ReagentCapacityInput &input, double *volume, QString *message)
{
    // 原版只在真正改走几何时读取底径和角度；未参与计算的几何缺项不拒绝矩阵/零高度。
    if (!std::isfinite(input.configuredBottle.bottomDiameterMm) || input.configuredBottle.bottomDiameterMm<=0 ||
        !std::isfinite(input.configuredBottle.angleDegrees))
        return fail(message,QStringLiteral("本孔原瓶型的底径或角度无效，未采用默认几何参数。"));
    const double pi=3.14159265358979323846;
    const double diameter=input.configuredBottle.bottomDiameterMm;
    const double angle=input.configuredBottle.angleDegrees*pi/180;
    const double height=input.rawHeightMm;
    if (!std::isfinite(angle)) return fail(message,QStringLiteral("原配置角度转弧度后不是有限数。"));
    const double section=pi*std::pow(diameter*std::cos(angle)+diameter*std::sin(angle)*std::tan(angle),2)/4;
    const double threshold=diameter*std::sin(angle);
    const double calculated=height<=threshold ? section*height/2
        : section*height*3/2-section*diameter*std::sin(angle);
    if (!std::isfinite(section) || !std::isfinite(threshold) || !std::isfinite(calculated))
        return fail(message,QStringLiteral("原配置瓶型的几何换算不是有限数。"));
    *volume=calculated;
    return true;
}

QString canonicalArea(const QString &area)
{
    if (area==QStringLiteral("0") || area==QStringLiteral("s_area")) return QStringLiteral("s_area");
    if (area==QStringLiteral("1") || area==QStringLiteral("r_area")) return QStringLiteral("r_area");
    return QString();
}

QString canonicalBottle(const QString &bottle)
{
    const QStringList aliases{QStringLiteral("7mL"),QStringLiteral("10mL"),QStringLiteral("15mL"),QStringLiteral("30mL"),QStringLiteral("50mL")};
    for (int index=0;index<aliases.size();++index)
        if (bottle==QString::number(index+1)) return aliases[index];
    bool numeric=false;
    bottle.toDouble(&numeric);
    // 原atoi对部分非整数数字文本也会给别名；本只读接口不为放行扩大这类未确认映射。
    return numeric ? QString() : bottle;
}
}

bool readReagentCapacityVersion(const QString &root,const QString &actualModel,
                                ReagentCapacityResult *result,QString *message)
{
    if (message) message->clear();
    if (!result) return fail(message,QStringLiteral("容量版本输出为空。"));
    *result=ReagentCapacityResult();
    if (actualModel.isEmpty()) return fail(message,QStringLiteral("实际机型尚未确认，未选择容量资料。"));
    result->actualModel=actualModel;
    result->relativeFile=actualModel==QStringLiteral("SN450") ? QStringLiteral("calibrationFile/ReagentAmount_450.json")
        : QStringLiteral("calibrationFile/ReagentAmount_600.json");
    if (!OriginalBusiness::workingRoot(root,message)) return false;
    QJsonObject document;
    return loadOriginalCapacity(root,result,&document,message);
}

bool calculateReagentCapacity(const QString &root, const QString &actualModel,
                              const ReagentCapacityInput &input, ReagentCapacityResult *result,
                              QString *message)
{
    if (message) message->clear();
    if (!result) return fail(message,QStringLiteral("容量换算输出为空。"));
    *result=ReagentCapacityResult();
    result->actualModel=actualModel;
    result->rawHeightMm=input.rawHeightMm;
    QString error;
    const auto reject=[&](const QString &reason)
    {
        result->failureReason=reason;
        return fail(message,reason);
    };
    if (actualModel.isEmpty()) return reject(QStringLiteral("实际机型尚未确认，未选择容量资料。"));
    result->relativeFile=actualModel==QStringLiteral("SN450") ? QStringLiteral("calibrationFile/ReagentAmount_450.json")
        : QStringLiteral("calibrationFile/ReagentAmount_600.json");
    const auto &configured=input.configuredBottle;
    const QString area=canonicalArea(input.area), bottle=canonicalBottle(input.reportedBottle);
    if (input.hole.isEmpty() || input.hole!=configured.hole || area.isEmpty() || area!=configured.area ||
        bottle.isEmpty() || bottle!=configured.name)
        return reject(QStringLiteral("探测孔位、区域或报文瓶型与本孔原配置不对应。"));
    if (!std::isfinite(input.rawHeightMm) || input.rawHeightMm<0 || input.rawHeightMm>=0xff00)
        return reject(QStringLiteral("原始毫米高度无效；本次明确拒绝，不沿原弱校验改写为0。"));
    // 最大量独立采用原hole->getMaxVol()快照，底径/角度只由几何分支校验。
    if (!std::isfinite(configured.maximumMicrolitres) || configured.maximumMicrolitres<0 ||
        configured.maximumMicrolitres>std::numeric_limits<int>::max())
        return reject(QStringLiteral("原孔位最大量无效，未进行容量换算。"));
    result->quantizedHeightHundredths=static_cast<int>(input.rawHeightMm*100);
    result->quantizedHeightMm=result->quantizedHeightHundredths/100.0;
    result->matrixMaximumMicrolitres=static_cast<int>(configured.maximumMicrolitres);
    if (!OriginalBusiness::workingRoot(root,&error)) return reject(error);
    QJsonObject document;
    if (!loadOriginalCapacity(root,result,&document,&error)) return reject(error);
    if (result->fileState==ReagentCapacityFileState::Missing)
        missingMatrix(result,QStringLiteral("对应实际机型的原容量文件明确不存在。"));
    else if (!matrixVolume(document,input,area==QStringLiteral("s_area") ? QStringLiteral("Area_S1") : QStringLiteral("Area_R1"),bottle,result,&error))
        return reject(error);
    if (input.rawHeightMm==0)
    {
        result->source=ReagentCapacitySource::ZeroHeight;
        // 原零高度仍保留矩阵运算结果，只跳过几何；不能额外强制容量为0。
        result->volumeMicrolitres=result->matrixVolumeMicrolitres;
    }
    else if (result->matrixVolumeMicrolitres==0)
    {
        if (result->matrixFailureReason.isEmpty())
            result->matrixFailureReason=QStringLiteral("原矩阵已计算且结果为0，按原非零高度条件改用本孔瓶型几何。");
        double volume=0;
        if (!geometryVolume(input,&volume,&error)) return reject(error);
        result->source=ReagentCapacitySource::Geometry;
        result->volumeMicrolitres=volume;
    }
    else
    {
        result->source=ReagentCapacitySource::Matrix;
        result->volumeMicrolitres=result->matrixVolumeMicrolitres;
    }
    result->displayVolumeMicrolitres=qMax(0.0,qMin(configured.maximumMicrolitres,result->volumeMicrolitres));
    result->available=true;
    return true;
}
}
