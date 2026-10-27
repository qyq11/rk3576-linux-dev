"""Read frozen formal source only; writes a new anonymous snapshot receipt."""
import getpass
import json
import os
from pathlib import Path
import runpy
import sys

root=Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work=root/'temp/sn100_host/qc_history_integration_20261026'
assert sys.argv[1]=='--output' and sys.argv[3:]==['probe_token','中文参数','--marker=ok'],sys.argv
output=Path(sys.argv[2])
assert output.is_relative_to(work/'integration_drivers/probes') and not output.exists()
assert getpass.getuser()=='qyq' and os.environ.get('WSL_DISTRO_NAME')=='Ubuntu-24.04'
inventory=runpy.run_path(str(root/'06_programs/sn100_host/shell/package_arm64.py'),run_name='sn100_probe_inventory')
current=inventory['snapshot'](root/'06_programs/sn100_host')
frozen=json.loads((work/'native/final_before.json').read_text())
assert current==frozen and len(current)==447
output.write_text(json.dumps(current,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'passed':True,'user':getpass.getuser(),'distro':os.environ.get('WSL_DISTRO_NAME'),
                  'uid':os.geteuid(),'argv':sys.argv[1:],'source_inputs':len(current)},ensure_ascii=False))
