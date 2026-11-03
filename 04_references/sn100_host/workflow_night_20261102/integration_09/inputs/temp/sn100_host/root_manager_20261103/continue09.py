"""保留首次补丁冲突，按已审内容生成公共文件的窄修订片。"""
from integrate09 import *

stage = WORK / 'preflight'
applied=[]
for index,(label,patch) in enumerate([
 ('qb',TASK/'qb_entry_revision_03_20261103/qb_entry_v3_on097e.patch'),
 ('amount',ROOT/'temp/task_reagent_amount_consistency_candidate_01/packaging_01/task_reagent_amount_on097e.patch'),
 ('lis_business',TASK/'lis_connection_candidate_20261103_01/lis_connection_business.patch'),
 ('lis_public',TASK/'lis_connection_candidate_20261103_01/lis_connection_public.patch'),
 ('lis_metadata',TASK/'lis_connection_candidate_20261103_01/metadata_addendum_01/lis_release_metadata.patch'),
 ('alarm_module',TASK/'current_alarm_candidate_20261103_01/current_alarm_module.patch')],1):
    applied.append({'index':index,'patch':patch.relative_to(ROOT).as_posix(),'sha256':sha(patch),
        'before_checkpoint':record(WORK/f'{index:02}_{label}_before.json'),
        'after_checkpoint':record(WORK/f'{index:02}_{label}_after.json')})

original=TASK/'current_alarm_candidate_20261103_01/current_alarm_public.patch'
excluded=[b'a/06_programs/sn100_host/shell/'+name+b' ' for name in (b'shell.pri',b'device_control_shell.cpp')]
blocks=original.read_bytes().split(b'diff --git ')
filtered=WORK/'alarm_public_nonoverlap.patch'
filtered.write_bytes(b''.join(b'diff --git '+b for b in blocks[1:] if not any(b.startswith(x) for x in excluded)))
applied.append(apply_one(stage,filtered,'alarm_public_narrow',7))

def narrow(name,new_content,label):
    old=(stage/PREFIX/name).read_bytes()
    patch=WORK/(label+'.patch')
    patch.write_bytes(''.join(difflib.unified_diff(old.decode('utf-8').splitlines(True),new_content.decode('utf-8').splitlines(True),
        fromfile='a/'+PREFIX+name,tofile='b/'+PREFIX+name)).encode('utf-8'))
    applied.append(apply_one(stage,patch,label,len(applied)+1))

name='shell/device_control_shell.cpp'
old=(stage/PREFIX/name).read_bytes()
eol=b'\r\n' if b'\r\n' in old else b'\n'
needle=b'#include "instrument/device_control.h"'+eol
assert old.count(needle)==1
new=old.replace(needle,needle+b'#include "alarm_history/original_current_alarm_state.h"'+eol)
needle=b'        if (deviceControl_->state().connected) deviceControl_->query(Sn100Protocol::ReadOnlyQuery::Alarm);'
assert new.count(needle)==1
new=new.replace(needle,'        // 当前告警已挂接时由唯一入口发送，避免同次点击重复Qk。'.encode('utf-8')+eol+
    b'        if (!currentAlarmState_ && deviceControl_->state().connected)'+eol+
    b'            deviceControl_->query(Sn100Protocol::ReadOnlyQuery::Alarm);')
narrow(name,new,'alarm_device_narrow')

name='shell/shell.pri'
old=(stage/PREFIX/name).read_bytes()
eol=b'\r\n' if b'\r\n' in old else b'\n'
needle=b'HEADERS += $$PWD/../alarm_history/original_alarm_receiver.h'
assert old.count(needle)==1
new=old.replace(needle,needle+eol+b'SOURCES += $$PWD/alarm_current_shell.cpp $$PWD/../alarm_history/original_current_alarm_state.cpp'+eol+
    b'HEADERS += $$PWD/../alarm_history/original_current_alarm_state.h')
narrow(name,new,'alarm_pri_narrow')

name='shell/package_arm64.py'
old=(stage/PREFIX/name).read_bytes()
eol=b'\r\n' if b'\r\n' in old else b'\n'
needle=b'            "lis_settings_scope":'
assert old.count(needle)==1
extra=(b'            "current_alarm_scope": {"query": "explicit_Qk", "receive": ["Qk", "QK"],'+eol+
 b'                                    "history_append_from_query": False, "QK_is_query_confirmation": False,'+eol+
 b'                                    "local_wait_hint_ms": 10000, "automatic_retry": False,'+eol+
 b'                                    "device_reset": False, "beep_ack": False, "verified_with_real_alarm": False},'+eol)
narrow(name,old.replace(needle,extra+needle),'current_alarm_release_scope')
save(WORK/'staged_patches.json',applied)
save(WORK/'staged_source_manifest.json',snapshot(stage/PREFIX))
print('STAGED_PATCHES='+str(len(applied)))
print('FORMAL_WRITTEN=0')
