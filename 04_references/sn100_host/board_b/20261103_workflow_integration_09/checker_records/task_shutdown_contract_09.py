"""08任务关闭合同的待接入增量；仅定义函数，不生成合同、不执行旧phase。

未来须由官方08冻结流程提供完整snapshot及已批准review源表，再调用本函数。
当前只作语法和源码审查，不能把本文件或Stage2当作已冻结运行合同。
"""
import copy
import hashlib
import re
from pathlib import Path

TASK_READY = 'TASK_REAGENT_SHUTDOWN_READY'
SOURCE_FILES = (
    'shell/main.cpp', 'shell/frmmain.cpp', 'shell/frmmain.h', 'shell/device_control_shell.cpp',
    'shell/task_reagent_context.h', 'shell/task_reagent_shell.cpp', 'shell/task_reagent_source.cpp',
    'shell/reagent_level_shell.cpp', 'reagents/original_reagent_level_worker.cpp',
    'reagents/original_reagent_level_worker.h', 'reagents/original_reagent_level_runtime.h',
    'reagents/original_reagent_level_task.cpp', 'reagents/reagents_page_task.cpp',
    'reagents/original_task_reagent_readback.cpp', 'samples/original_sample_store.cpp',
    'samples/original_task_reagent_receipt.h',
)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def without_comments(text):
    pattern = r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: m[0] if m[1] else re.sub(r'[^\n]', ' ', m[0]), text)


def compact(text):
    return re.sub(r'\s+', '', text)


def function(text, name, result_type):
    matches = list(re.finditer(r'\b' + result_type + r'\s+frmMain::' + re.escape(name)
                              + r'\s*\([^)]*\)\s*\{(.*?)^\}', text, re.S | re.M))
    require(len(matches) == 1, '任务生命周期函数缺失或形式变化，须重审：' + name)
    return matches[0]


def augment_task_shutdown_contract(base_contract, frozen_root, snapshot, approved_source_hashes, approved_review_sha256):
    """只补真实Task关门条件和独立诊断分类；不把未知结果判成业务成功。"""
    root = Path(frozen_root)
    require(root.is_absolute() and root.resolve() == root and root.name == 'sn100_host'
            and root.parent.name == 'frozen_09', '只接受官方归档生成的frozen_09/sn100_host')
    require(base_contract.get('schema') == 'sn100-shutdown-contract-09-v1'
            and re.fullmatch(r'[a-f0-9]{64}', base_contract.get('release_id', '')),
            '基础合同尚未绑定正式09发布')
    require(re.fullmatch(r'[a-f0-9]{64}', approved_review_sha256), '缺少实际已批准review的指纹')
    options = base_contract.get('runner_options', {})
    require({'--samples', '--catalog', '--instrument', '--instrument-control', '--original-db-root'} <= set(options),
            '当前增量只审完整原资料空闲QA，不能套用于未启用任务接线的模式')
    required = set(base_contract.get('required_markers', []))
    require({'SHELL_READY', 'SAMPLE_STORE_CLOSED', 'REAGENT_STORE_CLOSED', 'THREADS_STOPPED true',
             'REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false'} <= required,
            '基础关闭条件不完整')
    sources, hashes = {}, {}
    for name in SOURCE_FILES:
        path = root / name
        require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(root),
                '任务生命周期来源缺失或越界：' + name)
        raw = path.read_bytes()
        value = hashlib.sha256(raw).hexdigest()
        require(snapshot.get(name) == approved_source_hashes.get(name) == value,
                '任务来源不等于同批官方快照及已批准review：' + name)
        sources[name] = without_comments(raw.decode('utf-8'))
        hashes[name] = value
    require(base_contract.get('main_sha256') == hashes['shell/main.cpp'], '基础合同main不属于同批源码')

    task = sources['shell/task_reagent_shell.cpp']
    guard = function(task, 'requestTaskReagentShutdown', 'bool')
    guard_text = compact(guard[0])
    fragments = ('if(!taskReagentBridge_)returntrue;', 'auto&b=*taskReagentBridge_;',
                 'b.closing=true;', 'cancelTaskReagentContexts(', 'boolwaiting=!b.nReaders.isEmpty();',
                 'for(constauto&a:b.actions)if(a.dispatched||a.readerPending||!a.processed)waiting=true;',
                 'if(waiting)', 'b.closeRetryNeeded=true;', 'advanceTaskReagents();returnfalse;',
                 'qInfo()<<"TASK_REAGENT_SHUTDOWN_READY";returntrue;')
    require(all(value in guard_text for value in fragments)
            and guard_text.count('returntrue;') == 2 and guard_text.count('returnfalse;') == 1,
            '任务关门条件或返回分支变化，须重审')
    marker_matches = list(re.finditer(r'\bqInfo\s*\(\s*\)\s*<<\s*"TASK_REAGENT_SHUTDOWN_READY"\s*;', task))
    require(len(marker_matches) == 1 and guard.start() < marker_matches[0].start() < guard.end(),
            'Task READY不是唯一真实guard成功分支日志')
    require(guard_text.index('cancelTaskReagentContexts(') < guard_text.index('boolwaiting=')
            < guard_text.index('if(waiting)') < guard_text.index('qInfo()'), 'Task关门顺序变化，须重审')

    attach = compact(function(task, 'attachTaskReagents', 'void')[0])
    device_attach = compact(function(sources['shell/device_control_shell.cpp'], 'attachDeviceTaskStore', 'void')[0])
    require('std::make_shared<Bridge>()' in attach and 'taskReagentCommitObserved' in attach
            and 'Qt::QueuedConnection' in attach
            and 'deviceTaskStore_=store;attachTaskReagents();' in device_attach,
            '完整模式的任务Bridge接线未确认；无Bridge分支不打印READY，不能伪造通过')
    close = compact(function(sources['shell/frmmain.cpp'], 'closeEvent', 'void')[0])
    require('enterTaskReagentCloseAttempt();' in close and 'deferTaskReagentCatalogClose();' in close
            and 'if(!requestTaskReagentShutdown()){event->ignore();return;}' in close
            and close.index('requestTaskReagentShutdown()') < close.index('requestReagentLevelShutdown()')
            < close.rindex('event->accept();'), '真实窗口关闭未按当前已审Task/N顺序处理')
    advance = compact(function(task, 'advanceTaskReagents', 'void')[0])
    require('b.readerCloseIntent' in advance and 'b.closeRetryNeeded' in advance
            and 'QTimer::singleShot(0,this,[this](){window()->close();});' in advance,
            '专用读取或关闭收尾未返回真实外层窗口')
    main = compact(sources['shell/main.cpp'])
    require(all(value in main for value in ('sampleThread.wait();', 'reagentThread.wait();',
                                           '!sampleThread.isRunning()', '!reagentThread.isRunning()')),
            '任务复用的sample/reagent线程缺少实际最终等待依据')
    require('TASK_REAGENT_UNCONFIRMED_DISPLAY' in sources['reagents/reagents_page_task.cpp']
            and 'markTaskUnconfirmed' in sources['reagents/original_reagent_level_task.cpp']
            and 'REAGENT_LEVEL_N_READER_GUI_QUEUE_FAILED' in sources['shell/reagent_level_shell.cpp'],
            'Provider/Page或N专用读的诊断来源变化，须重审')

    result = copy.deepcopy(base_contract)
    if TASK_READY not in result['required_markers']:
        result['required_markers'].append(TASK_READY)
    result.setdefault('marker_evidence', []).append({
        'marker': TASK_READY, 'source': 'shell/task_reagent_shell.cpp',
        'line': task.count('\n', 0, marker_matches[0].start()) + 1,
        'requires_true': False, 'required': True,
        'purpose': '只证当前活动任务/专用读取的关门等待已结束；无Bridge时不打印。'})
    result['task_shutdown_guard'] = {
        'source_sha256': hashes, 'approved_review_sha256': approved_review_sha256, 'required_marker': TASK_READY,
        'no_bridge_returns_without_marker': True,
        'thread_ownership': '复用已有SampleStore/ReagentStore线程；Provider是页面持有的值对象，无新线程STOPPED。',
        'history_unknowns_may_remain': True, 'task_business_verified': False,
        'in_flight_task_single_outer_close_verified': False,
        'diagnostic_rules': {
            'transaction_or_display_uncertainty': ['TASK_REAGENT_UNCONFIRMED_DISPLAY', 'TASK_REAGENT_UNBOUND_FACT'],
            'reader_queue_failure': ['REAGENT_LEVEL_N_READER_GUI_QUEUE_FAILED'],
            'waiting_notice_not_cancellation_or_drain': ['TASK_REAGENT_WAIT_TIMEOUT'],
            'business_evidence_only': ['TASK_REAGENT_ARRIVED', 'TASK_REAGENT_OBSERVED', 'TASK_REAGENT_PROCESSED',
                                       'TASK_REAGENT_DISPLAY', 'TASK_REAGENT_HOLE', 'TASK_REAGENT_RETIRED'],
        },
        'diagnostic_policy': '关闭正常证明、事务事实、页面采用及未知状态分别报告；不能把所有false当关闭失败，也不能因READY而把未知改成功。',
    }
    return result
