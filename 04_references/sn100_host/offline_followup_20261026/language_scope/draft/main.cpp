#include "language_catalog_probe.h"

#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QStringList>
#include <cstddef>
#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("翻译资源抽样核验；不安装翻译器、不保存语言、不修改系统语言。"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("c"), QStringLiteral("catalog")},
        QStringLiteral("待只读核验的绝对.qm路径"), QStringLiteral("path")));
    parser.process(application);
    const LanguageCatalogInspection result = inspectLanguageCatalog(parser.value(QStringLiteral("catalog")));
    const QByteArray json = QJsonDocument(languageCatalogJson(result)).toJson(QJsonDocument::Indented);
    if (std::fwrite(json.constData(), 1, std::size_t(json.size()), stdout) != std::size_t(json.size())) return 4;
    if (!result.error.isEmpty()) return 2;
    return sampleCoverageComplete(result.samples) ? 0 : 3;
}
