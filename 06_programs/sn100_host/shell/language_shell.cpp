#include "frmmain.h"
#include "frmuserset.h"
#include "ui/language_setting_page.h"

void frmMain::attachLanguagePage(LanguageSettingPage *page)
{
    userSet_->attachLanguagePage(page);
}

LanguageSettingPage *frmMain::languagePage() const
{
    return userSet_->languagePage();
}
