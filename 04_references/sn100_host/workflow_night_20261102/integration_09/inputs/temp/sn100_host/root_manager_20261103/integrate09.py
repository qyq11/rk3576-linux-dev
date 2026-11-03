"""总控逐片整合：先在独立底稿核对，再按同一补丁顺序写正式文件。"""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TASK = ROOT / 'temp/sn100_host/workflow_night_20261102'
WORK = TASK / 'integration_09_review_01'
PROGRAM = ROOT / '06_programs/sn100_host'
BASE = TASK / 'integration_08_inventory_fix_01/source_manifest_after.json'
PREFIX = '06_programs/sn100_host/'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def save(p, value):
    with p.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, ensure_ascii=False, indent=2)
        f.write('\n')


def snapshot(tree):
    return {p.relative_to(tree).as_posix(): sha(p) for p in sorted(tree.rglob('*')) if p.is_file()}


def command(args, directory, name):
    result = subprocess.run(args, cwd=directory, capture_output=True)
    (WORK / (name + '.log')).write_bytes(result.stdout + result.stderr)
    (WORK / (name + '.exit')).write_text(str(result.returncode)+'\n', encoding='ascii')
    if result.returncode:
        raise RuntimeError('实际命令失败，保留现场：' + name)


def record(p):
    return {'path': p.relative_to(ROOT).as_posix(), 'sha256': sha(p), 'bytes': p.stat().st_size}


def apply_one(stage, patch, label, index):
    before = snapshot(stage / PREFIX)
    checkpoint = WORK / (f'{index:02}_{label}_before.json')
    save(checkpoint, before)
    command(['git', '-c', 'core.autocrlf=false', 'apply', '--check', str(patch)], stage, f'{index:02}_{label}_check')
    command(['git', '-c', 'core.autocrlf=false', 'apply', str(patch)], stage, f'{index:02}_{label}_apply')
    after = snapshot(stage / PREFIX)
    afterpoint = WORK / (f'{index:02}_{label}_after.json')
    save(afterpoint, after)
    return {'index': index, 'patch': patch.relative_to(ROOT).as_posix(), 'sha256': sha(patch),
            'before_checkpoint': record(checkpoint), 'after_checkpoint': record(afterpoint)}


def stage_all():
    assert sha(BASE) == '097e538263882ba23e405427140729f5316f0c839aeb1d4582045d658be9e768'
    base = json.loads(BASE.read_text(encoding='utf-8'))
    assert all(sha(PROGRAM / n) == h for n,h in base.items())
    assert not WORK.exists()
    WORK.mkdir()
    stage = WORK / 'preflight'
    for n,h in base.items():
        p=stage/PREFIX/n
        p.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(PROGRAM/n,p)
    shutil.copyfile(ROOT/'.gitattributes',stage/'.gitattributes')
    command(['git','init','--initial-branch=codex/integration09-review'],stage,'git_init')
    patches = [
      ('qb', TASK/'qb_entry_revision_03_20261103/qb_entry_v3_on097e.patch'),
      ('amount', ROOT/'temp/task_reagent_amount_consistency_candidate_01/packaging_01/task_reagent_amount_on097e.patch'),
      ('lis_business', TASK/'lis_connection_candidate_20261103_01/lis_connection_business.patch'),
      ('lis_public', TASK/'lis_connection_candidate_20261103_01/lis_connection_public.patch'),
      ('lis_metadata', TASK/'lis_connection_candidate_20261103_01/metadata_addendum_01/lis_release_metadata.patch'),
      ('alarm_module', TASK/'current_alarm_candidate_20261103_01/current_alarm_module.patch')]
    applied=[]
    for label,patch in patches:
        applied.append(apply_one(stage,patch,label,len(applied)+1))
    original=TASK/'current_alarm_candidate_20261103_01/current_alarm_public.patch'
    blocks=original.read_bytes().split(b'diff --git ')
    public=b''.join(b'diff --git '+b for b in blocks[1:] if not b.startswith(b'a/06_programs/sn100_host/shell/shell.pri '))
    filtered=WORK/'alarm_public_without_pri.patch'
    filtered.write_bytes(public)
    applied.append(apply_one(stage,filtered,'alarm_public',len(applied)+1))
    target=stage/PREFIX/'shell/shell.pri'
    old=target.read_bytes()
    needle=b'HEADERS += $$PWD/../alarm_history/original_alarm_receiver.h'
    assert old.count(needle)==1
    eol=b'\r\n' if b'\r\n' in old else b'\n'
    extra=eol+b'SOURCES += $$PWD/alarm_current_shell.cpp $$PWD/../alarm_history/original_current_alarm_state.cpp'+eol+b'HEADERS += $$PWD/../alarm_history/original_current_alarm_state.h'
    new=old.replace(needle,needle+extra)
    patch=WORK/'alarm_pri_narrow.patch'
    patch.write_bytes(''.join(difflib.unified_diff(old.decode('utf-8').splitlines(True),new.decode('utf-8').splitlines(True),fromfile='a/'+PREFIX+'shell/shell.pri',tofile='b/'+PREFIX+'shell/shell.pri')).encode('utf-8'))
    applied.append(apply_one(stage,patch,'alarm_pri',len(applied)+1))
    save(WORK/'staged_patches.json',applied)
    save(WORK/'staged_source_manifest.json',snapshot(stage/PREFIX))
    print('STAGED_PATCHES='+str(len(applied)))
    print('FORMAL_WRITTEN=0')


if __name__=='__main__':
    stage_all()
