from pathlib import Path
import shutil

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = root / 'temp/sn100_host/o03_4/arm_resume'
snapshot = work / 'source_snapshot_03.json'
shutil.copy2(root / 'temp/sn100_host/o03_4/rk_prepare/source_snapshot.json', snapshot)
source = (work / 'build_before_moc_fix.sh').read_text(encoding='utf-8')
lines = source.splitlines()
for index, line in enumerate(lines):
    if line.startswith('module_dir='):
        lines[index] = 'module_dir=' + str(root / '06_programs/sn100_host/shell')
        break
source = '\n'.join(lines) + '\n'
needle = 'for source in "${sources[@]}"; do\n    object=$build_dir/obj_'
capture = ('python3 "$module_dir/arm64_object_reuse.py" capture "' + str(work / 'compiled_objects_03.json') + '" "' + str(snapshot) + '" '
           '"$program_dir" "$build_dir" aarch64-linux-gnu-g++ "${RK_BUILD_TYPE:-Release}" '
           '--flags "${compile_flags[@]}" --sources "${sources[@]}"\nexit 0\n')
if source.count(needle) != 1:
    raise RuntimeError('Unexpected original builder layout')
(work / 'capture_original_compilation.sh').write_text(source.replace(needle, capture + needle), encoding='utf-8')
