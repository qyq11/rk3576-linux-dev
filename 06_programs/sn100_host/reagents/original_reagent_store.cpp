#include "reagent_store.h"
#include "original_reagent_editor.h"

void ReagentStore::initializeOriginal(QString root)
{
    if (!onWorker(QStringLiteral("初始化原试剂"))) return;
    close();
    std::unique_ptr<OriginalReagentEditor> editor(new OriginalReagentEditor);
    QString message;
    if (!editor->initialize(root,&message))
    { emit error(QStringLiteral("初始化原试剂"),message); return; }
    originalEditor_=std::move(editor);
    ready_=true;
    loadOriginal();
    emit ready();
}

void ReagentStore::setInstrumentBusy(bool busy)
{
    if (onWorker(QStringLiteral("更新仪器占用状态"))) instrumentBusy_=busy;
}

bool ReagentStore::originalMayEdit(const QString &operation)
{
    if (!onWorker(operation)) return false;
    if (!ready_ || !originalEditor_)
    { emit error(operation,QStringLiteral("原试剂资料未就绪。")); return false; }
    if (!session_.authenticated || !session_.actionReady || session_.role<0 || session_.role>5)
    { emit error(operation,QStringLiteral("当前账户没有试剂编辑权限。")); return false; }
    if (instrumentBusy_)
    { emit error(operation,QStringLiteral("仪器正在检测或有待核对命令，不能修改检测所用试剂。")); return false; }
    return true;
}

void ReagentStore::loadOriginal()
{
    if (!onWorker(QStringLiteral("读取原试剂")) || !originalEditor_) return;
    QString message; ReagentSnapshot snapshot; ReagentProjects projects;
    if (!originalEditor_->load(&snapshot,&projects,&message))
    { emit error(QStringLiteral("读取原试剂"),message); return; }
    emit originalProfileReady(snapshot.originalRoot,projects);
    emit loaded(snapshot);
}

void ReagentStore::finishOriginal(const QString &operation, bool success, const QString &message)
{
    if (!success) { emit error(operation,message); return; }
    emit succeeded(operation);
    loadOriginal();
}
