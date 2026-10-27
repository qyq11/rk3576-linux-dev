"""Prepare fresh02 only; preserves all original evidence and runs no build/GUI."""
import ast
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
ROOT = WORK.parents[2]
OLD = WORK/'post_platform_preparation'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def wsl(path):
    text = Path(path).as_posix()
    return '/mnt/c/' + text.split(':/',1)[1] if ':/' in text else text


def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def preserved():
    roots = (OLD,WORK/'post_standalone_01',WORK/'post_standalone_01_control')
    return {str(path.relative_to(ROOT).as_posix()):sha(path) for directory in roots
            for path in sorted(directory.rglob('*')) if path.is_file() and
            'runtime' not in path.relative_to(directory).parts}


def main():
    observed = preserved()
    prior = HERE/'preservation_before.json'
    if prior.exists():
        assert load(prior)['non_runtime_files_sha256'] == observed
    else:
        save(prior,{'non_runtime_files_sha256':observed,'runtime_values_read_or_hashed':False,
                    'scope':'Preservation observation now; never attributed to the old compilation time'})
    old_source = (OLD/'run_projects_modules.py').read_text(encoding='utf-8')
    tree = ast.parse(old_source)
    names = ('file_sha','make_words','build_inventory','verify_build')
    functions = '\n\n'.join(ast.get_source_segment(old_source,node) for name in names for node in tree.body
                            if isinstance(node,ast.FunctionDef) and node.name==name)
    old_guard = "    require(any(row['source_relative'] == 'projects/projects_store.cpp' for row in source_rows),\n            'The repaired ProjectsStore was not a source of this target')"
    assert functions.count(old_guard)==1
    functions = functions.replace(old_guard,
        "    graph = parse_graph(text, PROGRAM.resolve().as_posix(), build.resolve().as_posix())\n"
        "    role_proof = verify_role(graph)\n"
        "    require(set(graph['declared_sources']) == {row['source_relative'] for row in source_rows},\n"
        "            'Role graph differs from actually fingerprinted qmake sources')")
    functions = functions.replace("return inventory, {'sources': source_rows, 'objects': objects,",
        "return inventory, {'sources': source_rows, 'objects': objects, 'target_role': role_proof,")
    header = ('"""Fresh build inventory plus positive per-target business-source verification."""\n'
              'import os\nfrom pathlib import Path\nimport re\nimport shlex\n'
              'from common import PROGRAM, require, sha\nfrom build_roles import parse_graph, verify_role\n\n')
    (HERE/'build_verification.py').write_text(header+functions+'\n',encoding='utf-8')
    scripts = ('common.py','execute_stage.py','run_standalone.py','build_verification.py','build_roles.py')
    for name in (*scripts,'check_captured_roles.py'):
        ast.parse((HERE/name).read_text(encoding='utf-8'))
    source_before = load(WORK/'post_standalone_01/source_before.json')
    frozen = load(WORK/'native/final_before.json')
    assert source_before==frozen and len(frozen)==447
    assert sha(WORK/'native/final_before.json')=='1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'
    assert all(sha(ROOT/'06_programs/sn100_host'/name)==digest for name,digest in frozen.items())
    failure = load(WORK/'post_standalone_01/failure.json')
    stages = failure['actual_stages_completed']
    assert set(stages)=={'quality_control_qmake','quality_control_make','calibration_qmake','calibration_make'}
    assert all(row['exit_code']==0 for row in stages.values())
    assert not any((WORK/'post_standalone_01/logs'/name).exists() for name in
                   ('quality_control_run.command.json','calibration_run.command.json'))
    outputs = (WORK/'post_standalone_02',WORK/'post_standalone_02_control')
    assert not any(path.exists() for path in outputs)
    old_plan = load(OLD/'preparation.json')
    plan = {'schema':'sn100-post-platform-target-role-fix-preparation-v2',
        'status':'static_checks_pending','source_inputs':447,'source_snapshot_sha256':old_plan['source_snapshot_sha256'],
        'native_fresh_objects':210,'native_verification_sha256':old_plan['native_verification_sha256'],
        'native_makefile_sha256':old_plan['native_makefile_sha256'],'native_artifacts':old_plan['native_artifacts'],
        'executed_inputs_sha256':{wsl(HERE/name):sha(HERE/name) for name in scripts},
        'fresh_build_required':True,'old01_objects_or_binaries_reused':False,
        'reason_for_fresh02':'Calibration01 had actual make0 but no persisted post-build object/generated inventory before generic verifier raised. Current read-only hashes cannot be retroactively labeled build-time hashes.',
        'old01_actual_compilation_exits':{name:row['exit_code'] for name,row in stages.items()},
        'old01_stage_actual_exit':load(WORK/'post_standalone_01_control/verification.json')['actual_stage_exit'],
        'old01_GUI_invocations':0,'new02_build_or_GUI_invocations':0,
        'standalone_scope':'Only QC and calibration two independent apps, plus one catalog-owner initialization; seven phases total',
        'four_app_accounting':'Projects and reagents two apps belong to the separate projects stage; combined total four independent apps, not four here',
        'command':['wsl.exe','-d','Ubuntu-24.04','-u','qyq','--','python3','-B',wsl(HERE/'execute_stage.py'),
                   '--root-go','--stage','standalone','--name','post_standalone_02'],
        'new_output_paths_absent':list(map(wsl,outputs)),
        'original_preparation_failed01_control_measurement_preparation_unchanged':True}
    save(HERE/'preparation.json',plan)
    assert preserved()==observed
    print(json.dumps({'prepared_only':True,'fresh_build_required':True,'old4_compile_exits':plan['old01_actual_compilation_exits'],
                      'new02_build_or_GUI_invocations':0,'preserved_non_runtime_files':len(observed)},indent=2))


if __name__ == '__main__':
    main()
