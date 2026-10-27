#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QXmlStreamReader>
#include <QDebug>
#include <QTemporaryDir>
#include "original_ini.h"

static bool iniSelfCheck()
{
    QTemporaryDir directory;
    QFile fixture(directory.filePath(QStringLiteral("fixture.ini")));
    const QByteArray bytes = QStringLiteral("\ufeff[theme]\r\n  # ignored=123\r\n#中文注释\r\nname=中文名称\r\ncolor=#123abc\r\nnumber=0.125000\r\nempty=\r\nquoted=\"a,b#c\"\r\nlist=a, b\r\npath=C:\\\\demo\\\\file\r\ntext=left#right\r\n").toUtf8();
    if (!directory.isValid() || !fixture.open(QIODevice::WriteOnly) || fixture.write(bytes) != bytes.size()) return false;
    fixture.close();
    OriginalIni::Settings result;
    QString message;
    if (!OriginalIni::read(fixture.fileName(), &result, &message) || result.commentLines != 2 || result.values.size() != 8)
    {
        qWarning() << "ANONYMOUS_INI_FIXTURE_FAILED" << message;
        return false;
    }
    const auto &values = result.values;
    return values.value("theme/name").toString() == QStringLiteral("中文名称") &&
        values.value("theme/color").toString() == "#123abc" && values.value("theme/number").toString() == "0.125000" &&
        values.value("theme/empty").toString().isEmpty() && values.value("theme/quoted").toString() == "a,b#c" &&
        values.value("theme/list").toStringList() == QStringList({"a", "b"}) &&
        values.value("theme/path").toString() == "C:\\demo\\file" && values.value("theme/text").toString() == "left#right";
}

static bool checkIni(const QString &path, const QByteArray &bytes, QJsonObject *report)
{
    OriginalIni::Settings compatible;
    QString message;
    if (!OriginalIni::read(path, &compatible, &message)) return false;
    QSettings original(path, QSettings::IniFormat);
    original.setIniCodec("UTF-8");
    const auto originalKeys = original.allKeys();
    QMap<QString, QString> expected;
    QString section;
    for (QString line : QString::fromUtf8(bytes).split('\n'))
    {
        line = line.trimmed();
        if (line.startsWith(QChar(0xfeff))) line.remove(0, 1);
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2); continue; }
        const int equals = line.indexOf('=');
        if (equals <= 0 || section.isEmpty()) return false;
        const QString key = section + '/' + line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        // 本批4份原文件只有普通字符串和整段双引号字符串；不猜测未知转义。
        if (value.contains('\\') || value.contains(';') || value.startsWith('@')) return false;
        if (value.startsWith('"') && value.endsWith('"')) value = value.mid(1, value.size() - 2);
        else if (value.contains('"') || value.contains(',')) return false;
        if (expected.contains(key)) return false;
        expected.insert(key, value);
    }
    bool matches = compatible.values.keys() == expected.keys();
    int matched = 0;
    for (auto it = expected.cbegin(); it != expected.cend(); ++it)
    {
        const QVariant value = compatible.values.value(it.key());
        const QVariant oldValue = original.value(it.key());
        const bool same = originalKeys.contains(it.key()) && value.userType() == oldValue.userType() &&
            value == oldValue && value.toString() == it.value();
        matches = matches && same;
        if (same) ++matched;
    }
    report->insert("legacyQtStatus", int(original.status()));
    report->insert("legacyQtKeyCount", originalKeys.size());
    report->insert("iniKeyCount", compatible.values.size());
    report->insert("commentLinesHandled", compatible.commentLines);
    report->insert("matchedOriginalValuesAndTypes", matched);
    report->insert("allOriginalSettingsIdentical", matches);
    report->insert("iniParsed", matches);
    return matches;
}

// 只校验文件、表结构及可解析性；不查询或输出患者、口令、端点的记录值。
static bool regularInside(const QString &root, const QString &relative)
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char('\\')) ||
        relative.split(QLatin1Char('/')).contains(QStringLiteral(".."))) return false;
    const QFileInfo file(QDir(root).filePath(relative));
    if (!file.isFile() || file.isSymLink() || !file.canonicalFilePath().startsWith(root + QLatin1Char('/'))) return false;
    for (QFileInfo parent(file.absolutePath()); ; parent.setFile(parent.absolutePath()))
    {
        if (parent.isSymLink()) return false;
        if (parent.absoluteFilePath() == root) break;
        if (parent.absoluteFilePath() == parent.absolutePath()) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4) return 2;
    const QString root = QFileInfo(QString::fromLocal8Bit(argv[1])).canonicalFilePath();
    const QString manifestPath = QString::fromLocal8Bit(argv[2]);
    const QString output = QString::fromLocal8Bit(argv[3]);
    QFile manifest(manifestPath);
    if (root.isEmpty() || !QDir::isAbsolutePath(output) || !manifest.open(QIODevice::ReadOnly) || manifest.size() > 2 * 1024 * 1024) return 2;
    const auto document = QJsonDocument::fromJson(manifest.readAll());
    const auto entries = document.object().value(QStringLiteral("files")).toArray();
    if (document.object().value(QStringLiteral("format")).toString() != "sn100-original-files-v1" || entries.size() != 141) return 2;
    QJsonArray results;
    const bool iniFixturePassed = iniSelfCheck();
    int failures = iniFixturePassed ? 0 : 1, databases = 0, configurationWarnings = 0;
    for (int index = 0; index < entries.size(); ++index)
    {
        const auto entry = entries[index].toObject();
        const QString relative = entry.value("path").toString();
        bool ok = regularInside(root, relative);
        QFile file(QDir(root).filePath(relative));
        QByteArray bytes;
        if (ok && file.open(QIODevice::ReadOnly)) bytes = file.readAll();
        else ok = false;
        const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        ok = ok && bytes.size() == entry.value("bytes").toInt() && digest == entry.value("sha256").toString().toLatin1();
        file.close();
        QJsonObject result{{"path", relative}, {"shaMatches", ok}};
        if (ok && relative.endsWith(".db"))
        {
            ++databases;
            const QString connection = QStringLiteral("OriginalCheck-%1").arg(index);
            int tableCount = 0;
            bool integrity = false;
            {
                auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
                database.setDatabaseName(file.fileName());
                database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1000"));
                if (database.open())
                {
                    QSqlQuery query(database);
                    integrity = query.exec(QStringLiteral("PRAGMA quick_check")) && query.next() && query.value(0).toString() == "ok" && !query.next();
                    tableCount = database.tables(QSql::Tables).size();
                    query.finish();
                    database.close();
                }
            }
            QSqlDatabase::removeDatabase(connection);
            result.insert("readOnlyOpenedAndQuickCheck", integrity);
            result.insert("tableCount", tableCount);
            ok = ok && integrity && tableCount > 0;
        }
        else if (ok && (relative.endsWith(".json") || relative.endsWith(".cfg")))
        {
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(bytes, &error);
            ok = error.error == QJsonParseError::NoError && (json.isObject() || json.isArray());
            result.insert("jsonParsed", ok);
        }
        else if (ok && relative.endsWith(".ini"))
        {
            const bool parsed = checkIni(file.fileName(), bytes, &result);
            ok = ok && parsed;
            if (!parsed)
            {
                ++configurationWarnings;
                qWarning().noquote() << "CONFIGURATION_COMPATIBILITY_FAILED" << relative;
            }
        }
        else if (ok && relative.endsWith(".xml"))
        {
            QXmlStreamReader xml(bytes);
            while (!xml.atEnd()) xml.readNext();
            ok = !xml.hasError();
            result.insert("xmlParsed", ok);
        }
        result.insert("passed", ok);
        if (!ok) ++failures;
        results.append(result);
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << relative;
    }
    if (databases != 29) ++failures;
    const QJsonObject report{{"format", "sn100-original-files-rk-check-v1"}, {"root", root},
        {"fileCount", entries.size()}, {"databaseCount", databases}, {"failures", failures}, {"configurationWarnings", configurationWarnings}, {"files", results},
        {"configurationCompatibilityVerified", configurationWarnings == 0},
        {"iniAnonymousFixturePassed", iniFixturePassed},
        {"recordsOrCredentialsPrinted", false}, {"originalConnectionActivated", false}, {"mainApplicationOriginalReadWriteVerified", false}};
    QFile reportFile(output);
    if (!reportFile.open(QIODevice::WriteOnly) || reportFile.write(QJsonDocument(report).toJson()) <= 0) return 3;
    qInfo() << "RESULT" << "files" << entries.size() << "databases" << databases << "failures" << failures << "configurationWarnings" << configurationWarnings;
    return failures == 0 ? 0 : 1;
}
