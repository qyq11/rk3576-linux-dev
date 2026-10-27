"""Capture a completed full build under the repository's existing strict reuse contract."""
import json
from pathlib import Path
import subprocess
import hashlib
import shutil

root = Path(__file__).resolve().parents[3]
program = root / '06_programs/sn100_host'
out = root / 'temp/sn100_host/status_replay_20261024'
work = out / 'arm64'
build = work / 'arm64_build'
source = (program / 'shell/build.sh').read_text()
boundary = 'for source in "${sources[@]}"; do'
prefix = source[:source.rindex(boundary)]
old = 'module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)'
assert old in prefix
prefix = prefix.replace(old, 'module_dir=' + str(program / 'shell'))
prefix += '\npython3 "$module_dir/arm64_object_reuse.py" capture "$CAPTURE_MANIFEST" "$CAPTURE_SNAPSHOT" "$program_dir" "$build_dir" aarch64-linux-gnu-g++ Release --flags "${compile_flags[@]}" --sources "${sources[@]}"\n'
script = out / 'capture_arm_baseline.sh'
script.write_text(prefix)
baseline = out / 'arm_baseline_source_snapshot.json'
shutil.copy2(work / 'source_snapshot.json', baseline)
ui = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(build.glob('ui_*.h'))}
assert ui
(out / 'arm_baseline_ui_sha256.json').write_text(json.dumps(ui, indent=2) + '\n')
import os
environment = os.environ.copy()
environment.update(SN100_ARM64_SYSROOT=str(work / 'sysroot'), SN100_ARM64_BUILD_DIR=str(build),
                   CAPTURE_MANIFEST=str(out / 'arm_baseline_objects.json'), CAPTURE_SNAPSHOT=str(baseline))
environment.pop('SN100_ARM64_OBJECT_REUSE_MANIFEST', None)
subprocess.run(['bash', str(script), str(out / 'capture_only/no_binary')], env=environment, check=True)
after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(build.glob('ui_*.h'))}
assert ui == after, 'generated UI bytes changed during capture'
