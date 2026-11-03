#include "original_device_sync.h"
#include <QDir>

namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message=text;
    return false;
}
}

bool OriginalDeviceSync::initialize(const QString &root, QString *message)
{
    if (!repository_.initialize(root,message)) return false;
    root_=root; invalidate(); return true;
}

void OriginalDeviceSync::invalidate()
{
    if (activeUpdate_)
    {
        timeoutAwaitingSendConclusion_=false;
        activeUpdateState_.stage=QStringLiteral("connection-changed");
        activeUpdateState_.message=QStringLiteral("项目本地提交保留，旧连接等待已停止；没有自动转发旧意图。");
        recordUpdateState(activeUpdateState_);
    }
    ready_=false; waiting_=false; waitingForReagents_=false; projects_.clear(); projectIndex_=0;
    connection_=0;
}

bool OriginalDeviceSync::begin(quint64 connection, bool linuxNew, OriginalConfigurationStep *step, QString *message)
{
    if (!step || root_.isEmpty()) return fail(message,QStringLiteral("原配置同步未初始化。"));
    invalidate(); connection_=connection; linuxNew_=linuxNew;
    *step=OriginalConfigurationStep();
    if (!linuxNew)
    {
        QStringList localFiles;
        for (auto state : projectUpdates_)
        {
            if (!state.notice.verifiedSnapshot) continue;
            state.connection=connection_; state.stage=QStringLiteral("legacy-local");
            state.message=QStringLiteral("旧完整任务协议使用本地实际参数随任务发送，没有发送新协议Up。");
            recordUpdateState(state);
            localFiles.append(state.notice.originalFileName);
        }
        if (activeUpdate_ && activeUpdateState_.notice.verifiedSnapshot)
        {
            activeUpdateState_.connection=connection_; activeUpdateState_.stage=QStringLiteral("legacy-local");
            recordUpdateState(activeUpdateState_);
            activeUpdate_=false;
        }
        for (const auto &file : localFiles) { projectUpdates_.remove(file); updateAnchors_.remove(file); }
        // 原完整任务携带本次参数；Tu由设备请求时另行回应，不伪发新协议Up。
        ready_=true; step->complete=true; step->message=QStringLiteral("设备采用原完整任务协议，参数随任务下发。"); return true;
    }
    waiting_=true; waitingForReagents_=true;
    step->packets.append(QByteArrayLiteral("Ur"));
    step->message=QStringLiteral("正在读取设备实际试剂资料，随后同步原项目参数。");
    return true;
}

bool OriginalDeviceSync::reagentReplySaved(quint64 connection, OriginalConfigurationStep *step, QString *message)
{
    if (!step || connection!=connection_ || !waiting_ || !waitingForReagents_)
        return fail(message,QStringLiteral("该试剂返回不属于当前等待的配置同步。"));
    waitingForReagents_=false;
    if (!repository_.load(&projects_,message)) { invalidate(); return false; }
    initialUpdateEpoch_=nextUpdateIntent_;
    ProjectRecords records;
    for (const auto &project : projects_) records.append(project.record);
    setKnownProjectFiles(records);
    return nextProject(step,message);
}

bool OriginalDeviceSync::nextProject(OriginalConfigurationStep *step, QString *message)
{
    *step=OriginalConfigurationStep();
    if (projectIndex_>=projects_.size())
    {
        waiting_=false; ready_=true; step->complete=true; step->message=QStringLiteral("原项目参数已发送并收到对应项目回读，实际试剂资料已保存。");
        return true;
    }
    const auto &project=projects_[projectIndex_];
    const QString path=QDir(root_).filePath(QStringLiteral("project/")+project.record.originalFileName);
    if (project.record.originalFingerprint!=OriginalProjectRepository::databaseFingerprint(path,message))
    { invalidate(); return fail(message,QStringLiteral("同步期间原项目被修改，未继续发送旧参数，请重新同步。")); }
    QByteArray update;
    if (!OriginalProjectWire::updatePayload(project.wire,&update,message)) { invalidate(); return false; }
    step->packets.append(update);
    step->packets.append("UP["+project.record.name.toLatin1()+']');
    step->message=QStringLiteral("正在同步项目%1（%2/%3），等待设备回读。").arg(project.record.name).arg(projectIndex_+1).arg(projects_.size());
    return true;
}

bool OriginalDeviceSync::projectReply(const QByteArray &payload, quint64 connection, OriginalConfigurationStep *step, QString *message)
{
    if (!step || !connection || connection!=connection_)
        return fail(message,QStringLiteral("当前连接没有等待这条项目回读，未覆盖原库。"));
    *step=OriginalConfigurationStep();
    OriginalProjectWire::Project received;
    if (!OriginalProjectWire::parseDeviceProject(payload,&received,message)) { invalidate(); return false; }
    const QString name=received.base.value(QStringLiteral("project_en_name"));
    if (!waitingForReagents_)
    {
        bool known=false;
        for (const auto &project : projects_) if (project.record.name==name) known=true;
        if (!known)
        { invalidate(); return fail(message,QStringLiteral("设备返回未在本次同步清单中的项目%1，未创建原库或确认同步。").arg(name)); }
    }
    // Windows以返回项目名定位。Linux仍逐项确认：其他项目不能确认当前项，
    // 也不能以无关流量延长10秒等待。已确认项目重复返回时仍核发送字段。
    for (int i=0;i<projectIndex_;++i)
        if (projects_[i].record.name==name &&
            !OriginalProjectDeviceRepository::matchesSentParameters(projects_[i],received,message))
        { invalidate(); return false; }
    if (!waiting_ || waitingForReagents_ || projectIndex_>=projects_.size() || projects_[projectIndex_].record.name!=name)
    {
        step->ignored=true;
        step->message=QStringLiteral("忽略非当前等待项目%1的回读；未写库、未确认同步、未延长等待。").arg(name);
        if (message) *message=step->message;
        return true;
    }
    const auto sent=projects_[projectIndex_];
    OriginalDeviceProjectWriteResult written;
    const bool applied=OriginalProjectDeviceRepository::matchesSentParameters(sent,received,message) &&
        repository_.applyDeviceProject(payload,sent.record.name,sent.record.originalFingerprint,message,sent.record.originalFileName,&written);
    step->commitSeen=written.commitSeen;
    if (!applied)
    {
        const QString file=sent.record.originalFileName;
        bool idOk=false;
        const qint64 actualId=received.base.value(QStringLiteral("xuhao")).toLongLong(&idOk);
        if (written.commitSeen && idOk && actualId!=sent.record.id &&
            (projectUpdates_.contains(file) || (activeUpdate_ && activeUpdateState_.notice.originalFileName==file)))
        {
            const auto source=projectUpdates_.contains(file) ? projectUpdates_.value(file) : activeUpdateState_;
            recordUpdateState(referenceChangeState(source,sent.record.id,actualId,QString(),false));
        }
        invalidate(); return false;
    }
    // 回读允许原xuhao改变；按同一文件刷新真实编号与指纹，不用旧id重新找别的项目。
    projects_[projectIndex_]=written.project;
    reconcileInitialProject(sent,projects_[projectIndex_]);
    ++projectIndex_;
    const bool advanced=nextProject(step,message);
    step->commitSeen=written.commitSeen; // nextProject会重置step，不能抹掉前一个已提交UP。
    return advanced;
}

bool OriginalDeviceSync::canStart(bool linuxNew, QString *message) const
{
    if (projectIdentityReview_) return fail(message,projectIdentityReviewState_.message);
    return parametersReady(linuxNew,message);
}

bool OriginalDeviceSync::parametersReady(bool linuxNew, QString *message) const
{
    if (activeUpdate_ || !projectUpdates_.isEmpty() || unsupportedUpdate_)
        return fail(message,QStringLiteral("本地项目已保存，但仍有项目参数未获设备确认，未开始测试。"));
    if (!linuxNew) return true;
    if (!linuxNew_ || !ready_) return fail(message,QStringLiteral("原项目或实际试剂同步尚未完成，请先连接并完成同步后再开始。"));
    OriginalProjectDeviceRepository currentRepository;
    QList<OriginalDeviceProject> currentProjects;
    if (!currentRepository.initialize(root_,message) || !currentRepository.load(&currentProjects,message)) return false;
    if (currentProjects.size()!=projects_.size())
        return fail(message,QStringLiteral("项目清单在同步后已变化，请重新同步后再开始。"));
    for (const auto &project : projects_)
    {
        bool unchanged=false;
        for (const auto &current : currentProjects)
            if (project.record.originalFileName==current.record.originalFileName &&
                project.record.originalFingerprint==current.record.originalFingerprint)
            { unchanged=true; break; }
        if (!unchanged)
            return fail(message,QStringLiteral("项目参数在同步后已变化，请重新同步，不使用设备旧参数开始。"));
    }
    return true;
}

void OriginalDeviceSync::setKnownProjectFiles(const ProjectRecords &projects)
{
    knownProjectFiles_.clear();
    for (const auto &project : projects) knownProjectFiles_.insert(project.originalFileName);
}

bool OriginalDeviceSync::canRecordProjectSave(const QString &fileName, QString *message) const
{
    if (!knownProjectFiles_.contains(fileName))
        return fail(message,QStringLiteral("该文件尚未加入本次已读项目清单；请先核对原清单，校准草稿保留，尚未提交。"));
    QSet<QString> tracked;
    for (auto it=projectUpdates_.cbegin();it!=projectUpdates_.cend();++it) tracked.insert(it.key());
    if (activeUpdate_) tracked.insert(activeUpdateState_.notice.originalFileName);
    for (const auto &file : reservedProjectSaves_) tracked.insert(file);
    if (!tracked.contains(fileName) && tracked.size()>=knownProjectFiles_.size())
        return fail(message,QStringLiteral("已有待核对项目占满本次已读清单预算；校准草稿保留，尚未提交。"));
    return true;
}

bool OriginalDeviceSync::reserveProjectSave(quint64 ticket, const QString &fileName, QString *message)
{
    if (!ticket || reservedProjectSaves_.contains(ticket) || !canRecordProjectSave(fileName,message)) return false;
    reservedProjectSaves_.insert(ticket,fileName);
    return true;
}

void OriginalDeviceSync::recordUpdateState(const OriginalProjectUpdateState &state)
{
    updateStates_.append(state); // SampleStore在每次操作后立即取走，不作为历史队列保存。
}

OriginalProjectUpdateStates OriginalDeviceSync::takeProjectUpdateStates()
{
    const auto result=updateStates_;
    updateStates_.clear();
    return result;
}

OriginalProjectUpdateState OriginalDeviceSync::noteProjectSave(const OriginalProjectSaveNotice &notice)
{
    OriginalProjectUpdateState state;
    state.notice=notice; state.intent=++nextUpdateIntent_; state.connection=connection_;
    state.stage=notice.verifiedSnapshot ? QStringLiteral("queued") : QStringLiteral("unverified");
    state.message=notice.verifiedSnapshot ? QStringLiteral("本地项目已提交，设备参数尚未确认，等待当前连接允许同步。")
        : QStringLiteral("本地项目已经提交，但提交后版本未核实，未发送Up。");
    QString capacity;
    if (!notice.commitSeen || notice.root!=root_ || !canRecordProjectSave(notice.originalFileName,&capacity))
    {
        state.stage=QStringLiteral("unsupported");
        state.message=QStringLiteral("提交文件不在当前已读项目清单或数据根不一致；未将它当作单项目更新发送。需重新核对清单。");
        if (!unsupportedUpdate_) unsupportedUpdateState_=state;
        unsupportedUpdate_=true; recordUpdateState(state); return state;
    }
    if (connection_ && !linuxNew_ && ready_ && notice.verifiedSnapshot)
    {
        if (projectUpdates_.contains(notice.originalFileName))
        {
            auto old=projectUpdates_.take(notice.originalFileName); old.stage=QStringLiteral("superseded"); recordUpdateState(old);
            updateAnchors_.remove(notice.originalFileName);
        }
        state.stage=QStringLiteral("legacy-local"); state.ready=true;
        state.message=QStringLiteral("旧完整任务协议使用本地实际参数随任务发送，没有发送新协议Up。");
        recordUpdateState(state); return state;
    }
    const QString file=notice.originalFileName;
    QString anchor=notice.beforeFingerprint;
    if (projectUpdates_.contains(file))
    {
        auto old=projectUpdates_.value(file);
        anchor=updateAnchors_.value(file);
        if (old.notice.committedFingerprint!=notice.beforeFingerprint)
        {
            state.stage=QStringLiteral("unverified");
            state.message=QStringLiteral("连续保存的版本链不一致，提交保留，未猜测中间修改或发送Up。");
        }
        old.stage=QStringLiteral("superseded");
        old.message=QStringLiteral("尚未发送的旧保存已被新的明确保存取代，没有冒报设备确认。");
        recordUpdateState(old);
    }
    if (activeUpdate_ && activeUpdateState_.notice.originalFileName==file)
    {
        if (!projectUpdates_.contains(file) && activeUpdateState_.notice.committedFingerprint==notice.beforeFingerprint)
            anchor=updateAnchors_.value(file,activeUpdateState_.notice.beforeFingerprint);
        if (activeUpdateState_.stage==QStringLiteral("not-sent"))
        {
            if (activeUpdateState_.notice.committedFingerprint!=notice.beforeFingerprint)
            {
                state.stage=QStringLiteral("unverified");
                state.message=QStringLiteral("未发送意图之后出现未核实的中间版本；本次提交保留，未跨版本链发送Up。");
            }
            auto old=activeUpdateState_; old.stage=QStringLiteral("superseded"); recordUpdateState(old);
            anchor=updateAnchors_.value(file,old.notice.beforeFingerprint); activeUpdate_=false;
        }
        else if (activeUpdateState_.stage!=QStringLiteral("prepared") &&
                 activeUpdateState_.notice.committedFingerprint!=notice.committedFingerprint)
        {
            activeUpdateState_.stage=QStringLiteral("unknown");
            activeUpdateState_.message=QStringLiteral("Up在途后本地又提交了新版本；旧UP不能覆盖新值，结果仍待核对。");
            recordUpdateState(activeUpdateState_);
        }
    }
    updateAnchors_.insert(file,anchor); projectUpdates_.insert(file,state);
    recordUpdateState(state);
    return state;
}

bool OriginalDeviceSync::prepareProjectUpdate(quint64 connection, QList<QByteArray> *packets, QString *message)
{
    if (!packets || connection!=connection_ || !connection || !linuxNew_ || !ready_ || waiting_ || activeUpdate_ || unsupportedUpdate_)
        return false;
    QString file;
    quint64 earliest=0;
    for (auto it=projectUpdates_.cbegin();it!=projectUpdates_.cend();++it)
        if (it->stage==QStringLiteral("queued") && (!earliest || it->intent<earliest))
        { file=it.key(); earliest=it->intent; }
    if (file.isEmpty()) return false;
    auto state=projectUpdates_.value(file);
    if (state.connection && state.connection!=connection)
    {
        state.stage=QStringLiteral("connection-changed");
        state.message=QStringLiteral("这是旧连接上的保存意图，未自动转发到新连接；等待新初始同步实际核对文件。");
        projectUpdates_[file]=state; recordUpdateState(state); return false;
    }
    QList<OriginalDeviceProject> current;
    const OriginalDeviceProject *target=nullptr, *baseline=nullptr;
    bool loaded=repository_.load(&current,message);
    if (loaded)
        for (const auto &project : current)
            if (project.record.originalFileName==file) target=&project;
    for (const auto &project : projects_)
        if (project.record.originalFileName==file) baseline=&project;
    if (!loaded || !target || !baseline || target->record.originalFingerprint!=state.notice.committedFingerprint ||
        target->record.id!=state.notice.projectId || target->record.name!=state.notice.projectName ||
        baseline->record.originalFingerprint!=updateAnchors_.value(file))
    {
        state.stage=QStringLiteral("unverified");
        state.message=QStringLiteral("提交后项目身份、版本或原确认基线已变化，未发送Up；本地提交保留。");
        projectUpdates_[file]=state; recordUpdateState(state); return false;
    }
    QByteArray update;
    if (!OriginalProjectWire::updatePayload(target->wire,&update,message))
    {
        state.stage=QStringLiteral("not-sent"); state.message=message ? *message : QStringLiteral("原项目Up构造失败，未发送。");
        projectUpdates_[file]=state; recordUpdateState(state); return false;
    }
    activeUpdateProject_=*target; activeUpdateState_=state; activeUpdate_=true;
    activeSendConclusionKnown_=false; activeSendMayHaveQueued_=false; timeoutAwaitingSendConclusion_=false;
    projectUpdateClock_.start();
    projectUpdates_.remove(file);
    activeUpdateState_.connection=connection; activeUpdateState_.stage=QStringLiteral("prepared");
    activeUpdateState_.message=QStringLiteral("完整原Up参数已冻结，等待当前连接实际入队回执。");
    *packets={update,"UP["+target->record.name.toLatin1()+']'};
    recordUpdateState(activeUpdateState_); return true;
}

bool OriginalDeviceSync::projectUpdateWaiting() const
{
    return activeUpdate_ && (activeUpdateState_.stage==QStringLiteral("prepared") ||
                             activeUpdateState_.stage==QStringLiteral("waiting"));
}

void OriginalDeviceSync::projectUpdateSendResult(quint64 intent, quint64 connection, bool attempted, bool queued, const QString &message)
{
    if (!activeUpdate_ || activeUpdateState_.intent!=intent || activeUpdateState_.connection!=connection ||
        connection!=connection_) return;
    if (!attempted && queued) return; // 自相矛盾的回执不能作为“确未尝试”的证明。
    const bool awaiting=activeUpdateState_.stage==QStringLiteral("prepared") ||
        (activeUpdateState_.stage==QStringLiteral("unknown") && timeoutAwaitingSendConclusion_);
    if (!awaiting || activeSendConclusionKnown_ || activeSendMayHaveQueued_) return;
    if (!attempted)
    {
        // Root在调用任何send之前才可能给false；这条事实不因回执排队晚于10秒而丢失。
        activeSendConclusionKnown_=true; timeoutAwaitingSendConclusion_=false;
        auto proof=activeUpdateState_; proof.stage=QStringLiteral("not-attempted-proof");
        proof.message=QStringLiteral("本批次没有尝试进入发送队列，已核清仅因等待入队结论超时的未知来由。");
        recordUpdateState(proof);
    }
    else
    {
        activeSendConclusionKnown_=true; activeSendMayHaveQueued_=true; timeoutAwaitingSendConclusion_=false;
        if (activeUpdateState_.stage!=QStringLiteral("prepared") ||
            (projectUpdateClock_.isValid() && projectUpdateClock_.elapsed()>=10000))
        {
            activeUpdateState_.stage=QStringLiteral("unknown");
            activeUpdateState_.message=QStringLiteral("本批次已可能发送，但未在10秒期限内确认；保留真实发送未知，未自动重发。");
            recordUpdateState(activeUpdateState_); return;
        }
    }
    const QString file=activeUpdateState_.notice.originalFileName;
    const bool newer=projectUpdates_.contains(file) &&
        projectUpdates_[file].notice.committedFingerprint!=activeUpdateState_.notice.committedFingerprint;
    if (!attempted && projectUpdates_.contains(file))
    {
        activeUpdateState_.stage=QStringLiteral("superseded");
        activeUpdateState_.message=QStringLiteral("旧版本尚未尝试发送，已被新的明确保存取代；冻结旧参数没有入队。");
        recordUpdateState(activeUpdateState_); activeUpdate_=false;
        recordUpdateState(projectUpdates_.value(file)); return;
    }
    if (attempted && newer)
    {
        activeUpdateState_.stage=QStringLiteral("unknown");
        activeUpdateState_.message=QStringLiteral("Up可能已入队后本地又有新版本；保留新提交，旧UP不得覆盖，结果待核。");
        recordUpdateState(activeUpdateState_); return;
    }
    activeUpdateState_.stage=!attempted ? QStringLiteral("not-sent") : queued ? QStringLiteral("waiting") : QStringLiteral("unknown");
    activeUpdateState_.message=message;
    recordUpdateState(activeUpdateState_);
}

void OriginalDeviceSync::expireProjectUpdate(quint64 intent, quint64 connection)
{
    if (!activeUpdate_ || activeUpdateState_.intent!=intent || activeUpdateState_.connection!=connection || !projectUpdateWaiting()) return;
    timeoutAwaitingSendConclusion_=activeUpdateState_.stage==QStringLiteral("prepared") &&
        !activeSendConclusionKnown_ && !activeSendMayHaveQueued_;
    activeUpdateState_.stage=QStringLiteral("unknown");
    activeUpdateState_.message=QStringLiteral("单项目更新10秒未获完整匹配回读，执行结果待核，未自动重发。");
    recordUpdateState(activeUpdateState_);
}

bool OriginalDeviceSync::receiveProjectUpdate(const QByteArray &payload, quint64 connection, bool *handled, bool *saved, QString *message)
{
    *handled=connection==connection_ && !waiting_ && hasProjectUpdates(); *saved=false;
    if (!*handled) return true;
    if (!activeUpdate_ || activeUpdateState_.connection!=connection || activeUpdateState_.stage!=QStringLiteral("waiting"))
        return true; // 未发送、未知或旧等待的UP不写库、不确认、不续时。
    if (projectUpdateClock_.isValid() && projectUpdateClock_.elapsed()>=10000)
    {
        expireProjectUpdate(activeUpdateState_.intent,connection);
        if (message) *message=activeUpdateState_.message;
        return false;
    }
    OriginalProjectWire::Project received;
    if (!OriginalProjectWire::parseDeviceProject(payload,&received,message))
    {
        activeUpdateState_.stage=QStringLiteral("unknown"); activeUpdateState_.message=message ? *message : QStringLiteral("项目UP格式无效。");
        recordUpdateState(activeUpdateState_); return false;
    }
    if (received.base.value(QStringLiteral("project_en_name"))!=activeUpdateState_.notice.projectName) return true;
    const auto sent=activeUpdateProject_;
    const auto completed=activeUpdateState_;
    OriginalDeviceProjectWriteResult written;
    bool ok=OriginalProjectDeviceRepository::matchesSentParameters(sent,received,message) &&
        repository_.applyDeviceProject(payload,sent.record.name,sent.record.originalFingerprint,message,sent.record.originalFileName,&written);
    *saved=written.commitSeen;
    const bool expired=projectUpdateClock_.isValid() && projectUpdateClock_.elapsed()>=10000;
    if (!ok || !written.verifiedSnapshot || expired)
    {
        bool idOk=false;
        const qint64 actualId=received.base.value(QStringLiteral("xuhao")).toLongLong(&idOk);
        if (written.commitSeen && idOk && actualId!=sent.record.id)
            recordUpdateState(referenceChangeState(completed,sent.record.id,actualId,
                written.verifiedSnapshot ? written.project.record.originalFingerprint : QString(),false));
        activeUpdateState_.stage=QStringLiteral("unknown");
        activeUpdateState_.message=expired
            ? written.commitSeen ? QStringLiteral("项目UP已经提交，但完整确认超过10秒；实际提交保留，结果仍待核，未自动重发。")
                                 : QStringLiteral("单项目确认已超过10秒，未提交迟到UP，结果仍待核。")
            : !message || message->isEmpty() ? QStringLiteral("UP提交后的同文件快照未能确认，本地提交和待核对事实保留。") : *message;
        recordUpdateState(activeUpdateState_); return false;
    }
    const auto &confirmed=written.project;
    for (auto &project : projects_)
        if (project.record.originalFileName==sent.record.originalFileName) project=confirmed;
    activeUpdate_=false; *saved=true;
    auto final=completed; final.stage=QStringLiteral("confirmed"); final.confirmedFingerprint=confirmed.record.originalFingerprint;
    final.confirmedProjectId=confirmed.record.id;
    final.message=QStringLiteral("对应项目UP已完整核对并保存，确认指纹已更新。");
    if (confirmed.record.id!=sent.record.id)
        final=referenceChangeState(final,sent.record.id,confirmed.record.id,confirmed.record.originalFingerprint,true);
    const QString file=sent.record.originalFileName;
    if (projectUpdates_.contains(file) && projectUpdates_[file].notice.committedFingerprint==sent.record.originalFingerprint)
    {
        auto same=projectUpdates_.take(file); same.connection=connection; same.stage=final.stage;
        same.confirmedFingerprint=final.confirmedFingerprint; same.confirmedProjectId=final.confirmedProjectId;
        same.message=final.message; recordUpdateState(same);
    }
    if (!projectUpdates_.contains(file)) updateAnchors_.remove(file);
    QString readiness;
    final.ready=parametersReady(true,&readiness);
    if (!final.ready && !readiness.isEmpty()) final.message+=readiness;
    recordUpdateState(final); return true;
}

OriginalProjectUpdateState OriginalDeviceSync::referenceChangeState(const OriginalProjectUpdateState &source,
    qint64 oldId,qint64 newId,const QString &fingerprint,bool parametersConfirmed)
{
    auto state=source;
    state.connection=connection_; state.stage=parametersConfirmed ? QStringLiteral("identity-changed") : QStringLiteral("identity-review");
    state.confirmedFingerprint=fingerprint; state.confirmedProjectId=newId;
    state.message=(parametersConfirmed ? QStringLiteral("项目参数已确认") : QStringLiteral("项目UP已经提交，完整确认仍待核"))+
        QStringLiteral("，但原项目编号由%1变为%2；预设及草稿引用待核，未改写架/任务，未开始测试。").arg(oldId).arg(newId);
    if (!projectIdentityReview_) projectIdentityReviewState_=state;
    projectIdentityReview_=true;
    return state;
}

void OriginalDeviceSync::reconcileInitialProject(const OriginalDeviceProject &sent, const OriginalDeviceProject &confirmed)
{
    const QString file=sent.record.originalFileName;
    const bool savedFile=projectUpdates_.contains(file) || (activeUpdate_ && activeUpdateState_.notice.originalFileName==file);
    if (savedFile && sent.record.id!=confirmed.record.id)
    {
        OriginalProjectUpdateState state=projectUpdates_.contains(file) ? projectUpdates_.value(file) : activeUpdateState_;
        recordUpdateState(referenceChangeState(state,sent.record.id,confirmed.record.id,confirmed.record.originalFingerprint,true));
    }
    if (projectUpdates_.contains(file) && (projectUpdates_[file].intent<=initialUpdateEpoch_ ||
        projectUpdates_[file].notice.committedFingerprint==sent.record.originalFingerprint))
    {
        auto state=projectUpdates_.take(file); state.connection=connection_; state.stage=QStringLiteral("confirmed-initial");
        state.confirmedFingerprint=confirmed.record.originalFingerprint;
        state.confirmedProjectId=confirmed.record.id;
        state.message=QStringLiteral("新初始同步已实际确认当前文件版本，没有转发旧保存队列。"); recordUpdateState(state);
        updateAnchors_.remove(file);
    }
    if (activeUpdate_ && activeUpdateState_.notice.originalFileName==file && activeUpdateState_.intent<=initialUpdateEpoch_)
    {
        activeUpdateState_.connection=connection_; activeUpdateState_.stage=QStringLiteral("reconciled-initial");
        activeUpdateState_.confirmedFingerprint=confirmed.record.originalFingerprint;
        activeUpdateState_.confirmedProjectId=confirmed.record.id;
        activeUpdateState_.message=QStringLiteral("旧更新停止等待；新初始同步已确认当前参数，不据此声称旧Up当时执行成功。");
        recordUpdateState(activeUpdateState_); activeUpdate_=false;
    }
    if (unsupportedUpdate_ && unsupportedUpdateState_.notice.originalFileName==file && unsupportedUpdateState_.intent<=initialUpdateEpoch_)
        unsupportedUpdate_=false;
    OriginalProjectUpdateState baseline;
    baseline.connection=connection_; baseline.stage=QStringLiteral("initial-file-confirmed");
    baseline.notice.root=root_; baseline.notice.originalFileName=file; baseline.notice.committedFingerprint=sent.record.originalFingerprint;
    baseline.confirmedFingerprint=confirmed.record.originalFingerprint; baseline.confirmedProjectId=confirmed.record.id;
    recordUpdateState(baseline);
}
