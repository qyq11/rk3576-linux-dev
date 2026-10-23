#include "report_render.h"
#include "qtrpt.h"

#include <QDebug>
#include <QFile>
#include <QFontDatabase>
#include <QRawFont>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

namespace
{
const QString kBindingName = QStringLiteral("historyReportValueBinding");
const QString kFallbackFamily = QStringLiteral("Noto Sans CJK SC");

bool literalText(const QString &text)
{
    // QtRPT replaces data placeholders before interpreting <expressions> and
    // [fields]. Reject those reserved delimiters rather than execute input
    // as script, silently remove it, or change stored patient information.
    return !text.contains(QChar('<')) && !text.contains(QChar('>'))
        && !text.contains(QChar('[')) && !text.contains(QChar(']'))
        && !text.contains(QChar('\n')) && !text.contains(QChar('\r'));
}

bool validateSnapshot(const HistoryPrintDocument &document, HistoryPrintDocument *snapshot, QString *error)
{
    if (!HistoryReportDocument::fromJson(HistoryReportDocument::toJson(document), snapshot, error))
        return false;
    for (auto header = snapshot->headers.cbegin(); header != snapshot->headers.cend(); ++header)
        if (!literalText(header.value()))
        {
            *error = QStringLiteral("报告头字段%1含模板控制符<>[]或换行，不能按原模板安全输出；未改变原始信息。")
                .arg(header.key());
            return false;
        }
    for (int index = 0; index < snapshot->rows.size(); ++index)
        for (auto field = snapshot->rows.at(index).cbegin(); field != snapshot->rows.at(index).cend(); ++field)
            if (!literalText(field.value()))
            {
                *error = QStringLiteral("报告第%1行字段%2含模板控制符<>[]或换行，不能按原模板安全输出。")
                    .arg(index + 1).arg(field.key());
                return false;
            }
    return true;
}

bool hasChineseGlyph(const QString &family)
{
    const QRawFont font = QRawFont::fromFont(QFont(family));
    return font.isValid() && font.supportsCharacter(0x68c0); // 检
}

bool validateFieldGlyphs(const QDomElement &element, const QString &staticText,
                         const QStringList &parameters, const HistoryPrintDocument &document,
                         QString *error)
{
    QFont font(element.attribute(QStringLiteral("fontFamily")),
               element.attribute(QStringLiteral("fontSize")).toInt());
    font.setBold(element.attribute(QStringLiteral("fontBold")).toInt() != 0);
    font.setItalic(element.attribute(QStringLiteral("fontItalic")).toInt() != 0);
    const QRawFont raw = QRawFont::fromFont(font);
    if (!raw.isValid())
    {
        *error = QStringLiteral("报告字段%1没有可用字体。").arg(element.attribute(QStringLiteral("name")));
        return false;
    }
    const auto check = [&](const QString &text) {
        for (uint character : text.toUcs4())
        {
            if (character == '\t') continue; // Layout spacing, not a printed glyph.
            if (!raw.supportsCharacter(character))
            {
                // Log the field and code point only, never patient text. Do
                // not rely on an unknown platform fallback to hide .notdef.
                *error = QStringLiteral("报告字段%1的字体%2缺少字形U+%3，未生成可能含方块的报告。")
                    .arg(element.attribute(QStringLiteral("name")), font.family(),
                         QString::number(character, 16).toUpper().rightJustified(4, QChar('0')));
                return false;
            }
        }
        return true;
    };
    if (!check(staticText + QStringLiteral("0123456789"))) return false;
    for (const QString &parameter : parameters)
    {
        if (document.headers.contains(parameter))
        {
            if (!check(document.headers.value(parameter))) return false;
        }
        else
            for (const auto &row : document.rows)
                if (!check(row.value(parameter))) return false;
    }
    return true;
}

bool loadEmbeddedTemplate(QDomDocument *xml, const HistoryPrintDocument &document, QString *error)
{
    QFile file(QStringLiteral(":/history_report/report.xml"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
    {
        *error = QStringLiteral("内嵌原报告模板缺失或超出容量。");
        return false;
    }
    const QByteArray source = file.readAll();
    if (source.contains("<!DOCTYPE") || !xml->setContent(source)
        || xml->documentElement().tagName() != QStringLiteral("Reports"))
    {
        *error = QStringLiteral("内嵌原报告模板不是允许的独立XML格式。");
        return false;
    }
    const QSet<QString> allowedElements{QStringLiteral("Reports"), QStringLiteral("Script"),
        QStringLiteral("Report"), QStringLiteral("ReportBand"), QStringLiteral("TContainerField")};
    QSet<QString> allowedFields;
    for (auto it = document.headers.cbegin(); it != document.headers.cend(); ++it) allowedFields.insert(it.key());
    for (auto it = document.rows.first().cbegin(); it != document.rows.first().cend(); ++it) allowedFields.insert(it.key());
    QFontDatabase fonts;
    const QStringList families = fonts.families();
    QSet<QString> replacedFamilies;
    QList<QDomElement> elements{xml->documentElement()};
    int reportCount = 0;
    int masterBandCount = 0;
    for (int index = 0; index < elements.size(); ++index)
    {
        QDomElement element = elements.at(index);
        for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling())
            if (child.isElement()) elements.append(child.toElement());
        const QString tag = element.tagName();
        if (!allowedElements.contains(tag))
        {
            *error = QStringLiteral("内嵌模板包含未允许的元素%1；不启用外部数据源、插件或脚本。").arg(tag);
            return false;
        }
        if (tag == QStringLiteral("Script") && !element.text().trimmed().isEmpty())
        {
            *error = QStringLiteral("独立报告模板不允许执行脚本。");
            return false;
        }
        if (tag == QStringLiteral("Report"))
        {
            ++reportCount;
            if (!element.attribute(QStringLiteral("watermarkPixmap")).isEmpty())
            {
                *error = QStringLiteral("独立报告模板不允许外部水印资源。");
                return false;
            }
        }
        if (tag == QStringLiteral("ReportBand"))
        {
            if (element.attribute(QStringLiteral("type")) == QStringLiteral("MasterData")) ++masterBandCount;
            if (!element.attribute(QStringLiteral("dataSourceName")).isEmpty()
                || !element.attribute(QStringLiteral("groupingField")).isEmpty())
            {
                *error = QStringLiteral("独立报告模板不允许外部数据源或动态分组。");
                return false;
            }
        }
        if (tag != QStringLiteral("TContainerField"))
            continue;
        if (element.attribute(QStringLiteral("type")) != QStringLiteral("label")
            || !element.attribute(QStringLiteral("highlighting")).isEmpty()
            || element.attribute(QStringLiteral("printing")) != QStringLiteral("1")
            || !element.attribute(QStringLiteral("format")).isEmpty()
            || !element.attribute(QStringLiteral("input_format")).isEmpty())
        {
            *error = QStringLiteral("原报告切片只允许固定文字/数据字段，不启用额外脚本或外部内容。");
            return false;
        }
        QString value = element.attribute(QStringLiteral("value"));
        const QRegularExpression variable(QStringLiteral("\\[([^\\[\\]]+)\\]"));
        auto matches = variable.globalMatch(value);
        QStringList parameters;
        while (matches.hasNext())
        {
            const QString parameter = matches.next().captured(1);
            if (!allowedFields.contains(parameter))
            {
                *error = QStringLiteral("内嵌模板字段与独立报告数据契约不一致。");
                return false;
            }
            parameters.append(parameter);
        }
        value.remove(variable);
        value.remove(QStringLiteral("<Page>"));
        value.remove(QStringLiteral("<TotalPages>"));
        if (value.contains('<') || value.contains('>') || value.contains('[') || value.contains(']'))
        {
            *error = QStringLiteral("内嵌模板包含页码以外的动态表达式。");
            return false;
        }

        QString family = element.attribute(QStringLiteral("fontFamily"));
#ifdef Q_OS_WIN
        // Application font registration can expose the English or localized
        // family name. These are aliases of the same installed Windows face,
        // not a typography change; retain the template's size and geometry.
        QString alias;
        if (family == QStringLiteral("黑体") && !families.contains(family, Qt::CaseInsensitive)
            && families.contains(QStringLiteral("SimHei"), Qt::CaseInsensitive))
            alias = QStringLiteral("SimHei");
        else if (family == QStringLiteral("SimSun") && !families.contains(family, Qt::CaseInsensitive)
                 && families.contains(QStringLiteral("宋体"), Qt::CaseInsensitive))
            alias = QStringLiteral("宋体");
        if (!alias.isEmpty())
        {
            qInfo().noquote() << "HISTORY_REPORT_FONT_ALIAS" << family << "->" << alias;
            family = alias;
            element.setAttribute(QStringLiteral("fontFamily"), family);
        }
#endif
        if (!families.contains(family, Qt::CaseInsensitive) || !hasChineseGlyph(family))
        {
            if (!families.contains(kFallbackFamily, Qt::CaseInsensitive) || !hasChineseGlyph(kFallbackFamily))
            {
                *error = QStringLiteral("原报告字体不可用，且缺少支持中文的Noto Sans CJK SC替代字体。");
                return false;
            }
            element.setAttribute(QStringLiteral("fontFamily"), kFallbackFamily);
            replacedFamilies.insert(family);
        }
        if (!validateFieldGlyphs(element, value, parameters, document, error)) return false;
    }
    if (reportCount != 1 || masterBandCount != 1)
    {
        *error = QStringLiteral("原报告模板应只有一份报告和一个明细数据带。");
        return false;
    }
    for (const QString &family : replacedFamilies)
        qInfo().noquote() << "HISTORY_REPORT_FONT" << family << "->" << kFallbackFamily;
    return true;
}
}

bool prepareHistoryReport(QtRPT *engine, const HistoryPrintDocument &document, QString *error)
{
    QString localError;
    if (!error) error = &localError;
    error->clear();
    if (!engine || engine->thread() != QThread::currentThread())
    {
        *error = QStringLiteral("报告引擎必须存在并在其GUI所属线程准备。");
        return false;
    }
    HistoryPrintDocument snapshot;
    if (!validateSnapshot(document, &snapshot, error)) return false;
    QDomDocument xml;
    if (!loadEmbeddedTemplate(&xml, snapshot, error)) return false;
    if (!engine->loadReport(xml))
    {
        *error = QStringLiteral("原QtRPT未能加载内嵌报告模板。");
        return false;
    }
    // A reused engine must not retain callbacks for its previous patient.
    delete engine->findChild<QObject *>(kBindingName, Qt::FindDirectChildrenOnly);
    auto *binding = new QObject(engine);
    binding->setObjectName(kBindingName);
    QObject::connect(engine, &QtRPT::setDSInfo, binding, [snapshot, engine](DataSetInfo &info) {
        if (info.reportPage != 0 || info.dataSetNo != 1 || !info.dsName.isEmpty())
        {
            info.recordCount = 0;
            emit engine->renderError(QStringLiteral("报告数据带超出固定模板契约。"));
            return;
        }
        info.recordCount = snapshot.rows.size();
    });
    QObject::connect(engine, &QtRPT::setValue, binding,
        [snapshot, engine](int row, const QString &parameter, QVariant &value, int page) {
        if (page != 0)
        {
            value = QString();
            emit engine->renderError(QStringLiteral("报告模板页索引无效。"));
        }
        else if (snapshot.headers.contains(parameter))
            value = snapshot.headers.value(parameter);
        else if (row >= 0 && row < snapshot.rows.size() && snapshot.rows.at(row).contains(parameter))
            value = snapshot.rows.at(row).value(parameter);
        else
        {
            value = QString();
            emit engine->renderError(QStringLiteral("报告字段或记录索引超出快照契约。"));
        }
    });
    engine->setOfflinePreview(true);
    return true;
}
