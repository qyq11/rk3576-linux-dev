"""正式构建与归档全部完成后，用实际发布号构建匹配检查器，保存真实退出。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
here=Path(__file__).resolve().parent
root=here.parents[2]
work=root/'temp/sn100_host/workflow_night_20261102/integration_09_revision_05'
candidate=root/'temp/sn100_host/board_b/20261103_status_login_09_preparation_04'
archive=root/'07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09'
assert all((here/(s+'09_revision05_outer.exit')).read_text().strip()=='0' for s in ('build','stage','archive','collect','finish'))
assert json.loads((work/'finish_complete.json').read_text(encoding='utf-8'))['complete']
build=json.loads((work/'build_complete.json').read_text(encoding='utf-8'))
prov=json.loads((archive/'provenance.json').read_text(encoding='utf-8'))
release=build['release_id']
source=archive/('sn100_sources_'+release+'.tar.gz')
assert hashlib.sha256(source.read_bytes()).hexdigest()==prov['source_archive_sha256']
for row in (candidate/'candidate_sources_04.sha256').read_text().splitlines():
    digest,name=row.split('  ',1);assert hashlib.sha256((candidate/name).read_bytes()).hexdigest()==digest
def wsl(p):return '/mnt/c/'+p.relative_to(Path('C:/')).as_posix()
assert not (here/'checker09_revision05_outer.log').exists() and not (here/'checker09_revision05_outer.exit').exists()
command=['wsl','-d','Ubuntu-24.04','-u','qyq','--','python3',wsl(candidate/'execute_final_09_build.py'),'--execute',
         '--integration-directory',wsl(work),'--release-id',release,'--source-archive',wsl(source),'--source-archive-sha256',prov['source_archive_sha256']]
with (here/'checker09_revision05_outer.log').open('xb') as out:
    result=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
(here/'checker09_revision05_outer.exit').write_text(str(result.returncode)+'\n',encoding='ascii')
print('CHECKER09_ACTUAL_OUTER_EXIT='+str(result.returncode),flush=True)
sys.exit(result.returncode)
