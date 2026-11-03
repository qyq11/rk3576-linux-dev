#ifndef SN100_THEME_CONFIGURATION_H
#define SN100_THEME_CONFIGURATION_H
#include "original_runtime_settings.h"
#include <QColor>

// 独立测试资料也读取原theme_config.ini；只解释已有字段，不改写附件原文。
bool loadThemeConfiguration(const QString &path, OriginalRuntime::ThemeOptions *theme, QString *message);
QColor themeColour(const OriginalRuntime::ThemeOptions &theme, const QString &key, const QColor &fallback);
#endif
