#include "theme_configuration.h"
#include "original_ini.h"
#include <QFileInfo>
#include <QRegularExpression>

bool loadThemeConfiguration(const QString &path, OriginalRuntime::ThemeOptions *result, QString *message)
{
    const QFileInfo file(path);
    OriginalIni::Settings settings;
    if (!result || !file.isAbsolute() || !file.isFile() || file.isSymLink() ||
        !OriginalIni::read(path, &settings, message)) return false;
    const auto value = [&settings](const QString &name, const QString &fallback = QString())
    {
        for (auto it = settings.values.cbegin(); it != settings.values.cend(); ++it)
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) return it.value().toString().trimmed();
        return fallback;
    };
    OriginalRuntime::ThemeOptions theme;
    theme.shortName=value("theme/name"); theme.company=value("theme/company");
    theme.driverModel=value("theme/DriverModel"); theme.hospitalName=value("hospital/name");
    theme.logoSize=value("theme/Logosize", "82x82");
    bool valid=!theme.driverModel.isEmpty() && theme.driverModel.size()<=64 &&
        QRegularExpression("^[1-9][0-9]{0,2}x[1-9][0-9]{0,2}$").match(theme.logoSize).hasMatch();
    const auto number = [&value, &valid](const QString &key, int fallback, int minimum, int maximum)
    {
        bool ok=false; const int number=value(key,QString::number(fallback)).toInt(&ok);
        if (!ok || number<minimum || number>maximum) valid=false;
        return number;
    };
    theme.reagentPositions=number("theme/ReagentPanelChannelCount",20,20,42);
    valid=valid && (theme.reagentPositions==20 || theme.reagentPositions==42);
    theme.sampleHoles=number("theme/SampleHoleCount",8,1,128);
    theme.emergencyHoles=number("theme/EmergencyHoleCount",3,1,128);
    theme.testChannels=number("theme/ChannelCount",18,1,128);
    theme.reportTableType=number("theme/ReportTableType",1,0,1);
    const QStringList keys{"leftBaseColor","leftBtnColor","leftBtnFontColor","rightBaseColor","rightBtnColor",
        "rightBtnFontColor","matchColor","matchFontColor","matchHoverColor","kBtnNormalColor","kBtnHoverColor",
        "kBtnPressedColor","kReagentHoleSelectedColor","kCalibrationLineColor","kSampleSetBtnChecked",
        "kSampleSetWidget","kSampleSetRowSelected","kListWidgetRowSelected","kReportRowSelected"};
    for (const QString &key : keys)
    {
        QString colour=value("theme/"+key);
        if (colour.isEmpty()) continue;
        colour.replace('.',',');
        const QStringList components=colour.split(',');
        if (components.size()!=4) { valid=false; continue; }
        for (const QString &component : components)
        {
            bool ok=false; const int channel=component.trimmed().toInt(&ok);
            if (!ok || channel<0 || channel>255) valid=false;
        }
        theme.colours.insert(key,colour);
    }
    if (!valid)
    {
        if (message) *message=QStringLiteral("主题的机型、孔位数量、报告方式、标志尺寸或颜色不合法，原数据保留。");
        return false;
    }
    *result=theme;
    return true;
}

QColor themeColour(const OriginalRuntime::ThemeOptions &theme, const QString &key, const QColor &fallback)
{
    if (!theme.colours.contains(key)) return fallback;
    const QStringList channels=theme.colours.value(key).split(',');
    return channels.size()==4 ? QColor(channels[0].toInt(),channels[1].toInt(),channels[2].toInt(),channels[3].toInt()) : fallback;
}
