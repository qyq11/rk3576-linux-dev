"""把探测的开始/暂停限制放在对应动作门，保留查询/消音及原未知事实。"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
TASK = ROOT / 'temp/sn100_host/workflow_next_20261103'
OLD = TASK / 'root_combined_candidate_11_01'
NEW = TASK / 'root_combined_candidate_11_02'


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def line_end(line):
    return '\r\n' if line.endswith('\r\n') else '\n'


def unique(lines, text):
    hits = [i for i, value in enumerate(lines) if value.rstrip('\r\n') == text]
    assert len(hits) == 1, (text, hits)
    return hits[0]


def main():
    before_table = json.loads((OLD / 'sparse_after_17.json').read_text())
    for name, value in before_table.items():
        assert hashlib.sha256((OLD / 'candidate/06_programs/sn100_host' / name).read_bytes()).hexdigest() == value
    NEW.mkdir()
    for area in ('before', 'candidate'):
        for path in sorted((OLD / area).rglob('*')):
            if path.is_file():
                save(NEW / area / path.relative_to(OLD / area), path.read_bytes())
    source = NEW / 'candidate/06_programs/sn100_host/shell/device_control_shell.cpp'
    lines = source.read_bytes().decode('utf-8').splitlines(keepends=True)
    common = unique(lines, '    if (reagentProbe_ || reagentProbeUnknown_)')
    assert '未接受新的开始/暂停' in lines[common + 1]
    del lines[common:common + 2]
    at = unique(lines, 'QString frmMain::manualCommunicationBlocker(quint64 request) const')
    eol = line_end(lines[at])
    helper = [
        'QString frmMain::reagentProbeTaskControlBlocker() const', '{',
        '    // 探测只限制新的开始/暂停；查询和消音仍走原身份、连接及关闭守卫。',
        '    if (reagentProbe_ || reagentProbeUnknown_)',
        '        return tr("单孔探测尚未收尾或执行结果待核，未接受新的开始/暂停。");',
        '    return QString();', '}', '',
    ]
    lines[at:at] = [value + eol for value in helper]
    low = unique(lines, 'void frmMain::requestDevicePauseOrSilence(quint64 connection,bool pause,quint64 manualRequest)')
    high = next(i for i in range(low + 1, len(lines)) if lines[i].startswith('void frmMain::requestDeviceStart('))
    needle = '    if (reason.isEmpty() && manualRequest) reason=manualCommunicationBlocker(manualRequest);'
    hits = [i for i in range(low, high) if lines[i].rstrip('\r\n') == needle]
    assert len(hits) == 2
    for at in reversed(hits):
        lines.insert(at, '    if (reason.isEmpty() && pause) reason=reagentProbeTaskControlBlocker();' + line_end(lines[at]))
    at = unique(lines, '        reason=deviceControl_->pauseBlocker();')
    eol = line_end(lines[at])
    lines[at:at+1] = ['        reason=reagentProbeTaskControlBlocker();' + eol,
                      '        if (reason.isEmpty()) reason=deviceControl_->pauseBlocker();' + eol]
    at = unique(lines, '                           state.pending.isEmpty() && !state.outcomeUnknown;')
    lines.insert(at + 1, '    const QString probeTaskBlocker=reagentProbeTaskControlBlocker();' + line_end(lines[at]))
    at = unique(lines, '    ui->btnPause->setBtnDisabled(!available || state.status.mainStatus < 3 || state.status.mainStatus > 6);')
    lines[at] = '    ui->btnPause->setBtnDisabled(!available || !probeTaskBlocker.isEmpty() || state.status.mainStatus < 3 || state.status.mainStatus > 6);' + line_end(lines[at])
    at = unique(lines, '    ui->btnPause->setToolTip(deviceControl_->pauseBlocker());')
    lines[at] = '    ui->btnPause->setToolTip(probeTaskBlocker.isEmpty() ? deviceControl_->pauseBlocker() : probeTaskBlocker);' + line_end(lines[at])
    at = unique(lines, '    if (reagentProbe_ || reagentProbeUnknown_) return tr("单孔探测尚未收尾或执行结果待核。");')
    eol = line_end(lines[at])
    lines[at:at+1] = ['    const QString probeTaskBlocker=reagentProbeTaskControlBlocker();' + eol,
                      '    if (!probeTaskBlocker.isEmpty()) return probeTaskBlocker;' + eol]
    source.write_bytes(''.join(lines).encode('utf-8'))
    header = NEW / 'candidate/06_programs/sn100_host/shell/frmmain.h'
    lines = header.read_bytes().decode('utf-8').splitlines(keepends=True)
    at = unique(lines, '    QString deviceUserControlBlocker(quint64 connection, bool requireFresh = true) const;')
    lines.insert(at + 1, '    QString reagentProbeTaskControlBlocker() const;' + line_end(lines[at]))
    header.write_bytes(''.join(lines).encode('utf-8'))
    after = {name: hashlib.sha256((NEW / 'candidate/06_programs/sn100_host' / name).read_bytes()).hexdigest() for name in before_table}
    changed = sorted(name for name in after if after[name] != before_table[name])
    assert changed == ['shell/device_control_shell.cpp', 'shell/frmmain.h']
    result = {'format': 'sn100-root-probe-guard-scope-revision-v1',
              'prior_sparse_17': before_table, 'new_sparse_17': after,
              'changed': changed, 'formal_written': False, 'compiled': False, 'board_operated': False}
    save(NEW / 'guard_scope_revision_02.json', (json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    # 沿用已实跑的普通补丁流程，只移除前一修订的一次冲突处理，并指向新目录。
    packager = (ROOT / 'temp/sn100_host/resume_dispatch_20261103/finish_sparse_combination_11_01.py').read_text()
    prefix = packager[:packager.index('def main():')]
    remainder = packager[packager.index('    root_manifest ='):]
    derived = prefix + 'def main():\n' + remainder
    assert derived.count('root_combined_candidate_11_01') == 1
    derived = derived.replace('root_combined_candidate_11_01', 'root_combined_candidate_11_02')
    save(ROOT / 'temp/sn100_host/resume_dispatch_20261103/package_sparse_combination_11_02.py', derived.encode('utf-8'))
    print(json.dumps({'revision': str(NEW.relative_to(ROOT)), 'changed': changed,
                      'formal_written': False, 'compiled': False, 'board_operated': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
