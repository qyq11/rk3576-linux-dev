"""Windows preparation only: copy reviewed drivers, adapt bindings, parse AST. Never run them."""
import ast
import hashlib
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
OLD = ROOT / 'temp/sn100_host/qc_local_20261025/integration_drivers'
NATIVE = WORK / 'native'
FREEZE = '1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def wsl(path):
    return '/mnt/c/' + Path(path).as_posix().split(':/', 1)[1]


def replace(text, old, new, count=1):
    assert text.count(old) == count, (old, text.count(old), count)
    return text.replace(old, new)


def named_main(text, old_default, new_default):
    text = replace(text, "STAGE = WORK / '" + old_default + "'", "STAGE = WORK / '" + new_default + "'")
    needle = "    args = parser.parse_args()\n"
    return replace(text, needle, "    parser.add_argument('--name', default='" + new_default + "')\n" + needle +
        "    global STAGE\n    require(re.fullmatch(r'[A-Za-z0-9_-]+', args.name), 'Simple fresh attempt name required')\n"
        "    STAGE = WORK / args.name\n")


def main():
    original_names = ('common.py', 'run_standalone.py', 'run_projects_modules.py', 'run_response.py', 'run_resource.py')
    old_hashes = {wsl(OLD / name): sha(OLD / name) for name in original_names}
    root_inputs = tuple(WORK / 'integration_drivers' / name for name in
        ('common.py', 'preparation.json', 'run_suite.py', 'build_test_drivers.py'))
    preserved_root = {wsl(path): sha(path) for path in root_inputs}
    frozen = load(NATIVE / 'final_before.json')
    native = load(NATIVE / 'verification.json')
    assert len(frozen) == 447 and sha(NATIVE / 'final_before.json') == FREEZE
    assert native['passed'] and native['actual_exit'] == 0 and len(native['fresh_objects']) == 210
    assert native['snapshot_sha256'] == FREEZE
    snapshot = runpy.run_path(str(PROGRAM / 'shell/package_arm64.py'), run_name='post_platform_prepare_inventory')['snapshot']
    # Windows inventory keys use backslashes; only normalize this read-only view.
    observed = {name.replace('\\', '/'): digest for name, digest in snapshot(PROGRAM).items()}
    assert observed == frozen
    for name, row in native['fresh_objects'].items():
        assert row['created_after_start'] and sha(NATIVE / name) == row['sha256']
    assert sha(NATIVE / 'wsl_release/Makefile') == native['makefile_sha256']
    assert all(sha(NATIVE / 'wsl_release' / name) == digest for name, digest in native['artifacts'].items())
    source = {name: (OLD / name).read_text(encoding='utf-8') for name in original_names}

    common = replace(source['common.py'], "    verify_frozen(frozen)\n    report = load(NATIVE / 'verification.json')",
        "    verify_preparation()\n    verify_frozen(frozen)\n    report = load(NATIVE / 'verification.json')\n"
        "    require(len(report.get('fresh_objects', {})) == 210, 'Exactly 210 fresh native objects required')")
    common = replace(common, "    require(isinstance(frozen, dict) and frozen, 'Nonempty formal source freeze required')",
        "    require(isinstance(frozen, dict) and len(frozen) == 447 and sha(FREEZE) == '" + FREEZE + "',\n"
        "            'Exact completed447 freeze required; old426/432 evidence is invalid here')")
    common = common.replace('subprocess.Popen(record[', 'spawn_owned(record[')
    # Replace old cleanup: an exited leader must not shorten descendants' TERM grace period.
    tree = ast.parse(common)
    node = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'stop_owned_group')
    lines = common.splitlines(True)
    replacement = '''def stop_owned_group(process):
    result = {'owned_process_group': process.pid, 'signals': []}
    for sig in (signal.SIGTERM, signal.SIGKILL):
        if group_state(process.pid)['gone']:
            break
        try:
            os.killpg(process.pid, sig)
            result['signals'].append(sig.name)
        except ProcessLookupError:
            break
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            process.poll()
            if group_state(process.pid)['gone']:
                break
            time.sleep(.05)
    process.wait(timeout=5)
    result.update(actual_exit=process.returncode, group_after=group_state(process.pid))
    return result
'''
    common = ''.join(lines[:node.lineno-1]) + replacement + ''.join(lines[node.end_lineno:])
    common += (HERE / 'process_support.txt').read_text(encoding='utf-8')
    (HERE / 'common.py').write_text(common, encoding='utf-8')

    projects = named_main(source['run_projects_modules.py'], 'projects_modules_final', 'post_projects_modules_01')
    projects = projects.replace('subprocess.Popen(', 'spawn_owned(')
    projects = replace(projects, "\nSTAGE = WORK", "\nfrom common import spawn_owned\n\nSTAGE = WORK")
    projects = replace(projects, "EXPECTED_FREEZE = '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'",
        "EXPECTED_FREEZE = '" + FREEZE + "'")
    projects = replace(projects, "EXPECTED_STORE = 'b234dd9d2609fc1b424396a2d9244f4dc2a9b4929fb0f9dae46a12685141702f'",
        "EXPECTED_STORE = '" + frozen['projects/projects_store.cpp'] + "'")
    projects = projects.replace('len(frozen) == 426', 'len(frozen) == 447').replace('426-input/5123bf14', '447-input/1486686b').replace('source_426_', 'source_447_')
    projects = replace(projects, "            env, env_receipt = private_environment(runtime)\n",
        "            env, env_receipt = private_environment(runtime)\n"
        "            if expected is None:\n                env['QT_QPA_PLATFORM'] = 'xcb'\n"
        "                env_receipt['overrides']['QT_QPA_PLATFORM'] = 'xcb'\n")
    (HERE / 'run_projects_modules.py').write_text(projects, encoding='utf-8')

    standalone = named_main(source['run_standalone.py'], 'standalone_final', 'post_standalone_01')
    standalone = replace(standalone, 'import os\n', 'import os\nimport re\n')
    standalone = replace(standalone, '\nSTAGE = WORK',
        '\nfrom common import spawn_owned, clean_environment\nfrom run_projects_modules import verify_build, build_inventory\n\nSTAGE = WORK')
    standalone = standalone.replace('subprocess.Popen(', 'spawn_owned(')
    standalone = replace(standalone, "    build_env = dict(os.environ)\n    for key in ('QMAKESPEC', 'QMAKEPATH', 'QMAKEFEATURES', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH'):\n        build_env.pop(key, None)\n    gui_env = dict(build_env, **selected_env)",
        "    build_env, clean_receipt = clean_environment(runtime, 'xcb')\n    gui_env = dict(build_env)\n"
        "    save(STAGE / 'private_environment.json', clean_receipt)")
    standalone = replace(standalone, '    modules = {}\n', '    modules = {}\n    module_inventories = {}\n')
    standalone = replace(standalone, "            qmake_name, make_name = name + '_qmake', name + '_make'",
        "            build_started_ns = time.time_ns()\n            qmake_name, make_name = name + '_qmake', name + '_make'")
    standalone = replace(standalone, "            require(binary.is_file(), 'Successful make did not create ' + target)",
        "            require(binary.is_file(), 'Successful make did not create ' + target)\n"
        "            inventory, proof = verify_build(build_dir, binary, frozen, build_started_ns)\n"
        "            module_inventories[name] = inventory\n            save(logs / (name + '_build_inventory.json'), inventory)\n"
        "            save(logs / (name + '_build_proof.json'), proof)")
    standalone = replace(standalone, '质控本地数据已读取；真实曲线和规则尚未启用。',
        '质控元数据已读取；所选项目历史另行只读加载，临床规则未启用。')
    standalone = replace(standalone, "        checks = {'source_unchanged': source_before == source_after == frozen,",
        "        checks = {'fresh_standalone_objects_unchanged': all(build_inventory(STAGE / row['binary'].rsplit('/',1)[0], STAGE / row['binary']) == module_inventories[name] for name, row in modules.items()),\n"
        "            'source_unchanged': source_before == source_after == frozen,")
    (HERE / 'run_standalone.py').write_text(standalone, encoding='utf-8')

    response = replace(source['run_response.py'], 'import re\n', 'import re\nimport shutil\nfrom run_projects_modules import verify_qt\nfrom common import clean_environment\n')
    response = replace(response, "default='response_01'", "default='post_response_01'")
    response = replace(response, "proof = load(DRIVERS / 'binding_proof.json')", "proof_sha = sha(DRIVERS / 'binding_proof.json')\nproof = load(DRIVERS / 'binding_proof.json')")
    needle = "require(sha(PROGRAM / 'tests' / (driver+'.cpp')) == record['source_sha256'], 'Response source changed')"
    response = replace(response, needle, needle + "\nfor path, row in proof['native_objects'].items():\n"
        "    require(sha(path) == row['object_sha256'] and sha(row['source']) == row['source_sha256'], 'Response production object/source changed')\n"
        "for suffix, key in (('.o','object_sha256'), ('.moc','moc_sha256')):\n"
        "    path = DRIVERS / '.obj' / (driver+'.o') if suffix == '.o' else DRIVERS / '.moc' / (driver+'.moc')\n"
        "    require(sha(path) == record[key], 'Response test object/moc changed')\n"
        "bound_binary = binary\nlocal = out / 'executable'\nlocal.mkdir()\n"
        "binary = local / driver\nshutil.copy2(bound_binary, binary)\n"
        "require(sha(binary) == record['binary_sha256'], 'Exact response executable copy mismatch')")
    response = replace(response, "env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',\n           SN100_WSL_IME='off', SN100_QC_EVIDENCE=str(out / 'evidence'))",
        "host_runtime = out / 'host_runtime'\nhost_runtime.mkdir()\nenv, clean_receipt = clean_environment(host_runtime, 'xcb')\n"
        "env.update(SN100_WSL_IME='off', SN100_QC_EVIDENCE=str(out / 'evidence'))\nsave(out / 'private_environment.json', clean_receipt)")
    response = replace(response, "metrics = load(out/'evidence/metrics.json')", "qt = verify_qt(out, 3)\nmetrics = load(out/'evidence/metrics.json')")
    response = replace(response, "verify_frozen(frozen)\nsave(out/'verification.json'", "verify_native(frozen)\n"
        "require(sha(DRIVERS / 'binding_proof.json') == proof_sha and sha(bound_binary) == record['binary_sha256'] and sha(binary) == record['binary_sha256'], 'Response input/copy changed')\n"
        "save(out/'verification.json'")
    response = replace(response, "'metrics_sha256':sha(out/'evidence/metrics.json')", "'qt_totals':qt['totals'],'bound_proof_sha256':proof_sha,'exact_binary_copy_sha256':sha(binary),\n     'metrics_sha256':sha(out/'evidence/metrics.json')")
    (HERE / 'run_response.py').write_text(response, encoding='utf-8')

    resource = replace(source['run_resource.py'], 'from common import load,', 'from common import spawn_owned, clean_environment, load,')
    resource = resource.replace('subprocess.Popen(', 'spawn_owned(')
    resource = replace(resource, "default='resource_01'", "default='post_resource_01'")
    resource = replace(resource, "        start = time.monotonic()\n        with (driver / 'observer_driver.log')",
        "        host_environment = runtime / 'host-environment'\n        host_environment.mkdir()\n"
        "        observer_env, clean_receipt = clean_environment(host_environment, 'xcb')\n"
        "        save(driver / 'private_environment.json', clean_receipt)\n"
        "        start = time.monotonic()\n        with (driver / 'observer_driver.log')")
    resource = replace(resource, 'env=dict(os.environ, **selected_env),', 'env=observer_env,')
    resource = replace(resource, "info = {'schema': 'sn100-four-windows-resource-v1', 'checks': checks,",
        "info = {'schema': 'sn100-post-platform-resource-v2', 'passed': all(checks.values()), 'checks': checks,")
    (HERE / 'run_resource.py').write_text(resource, encoding='utf-8')

    scripts = ('common.py', 'execute_stage.py', 'run_standalone.py', 'run_projects_modules.py', 'run_response.py', 'run_resource.py')
    for name in scripts:
        ast.parse((HERE / name).read_text(encoding='utf-8'))
    defaults = {'standalone':'post_standalone_01','projects':'post_projects_modules_01',
                'response':'post_response_01','resource':'post_resource_01'}
    commands = {stage:['wsl.exe','-d','Ubuntu-24.04','-u','qyq','--','python3','-B',wsl(HERE/'execute_stage.py'),
                      '--root-go','--stage',stage,'--name',name] for stage,name in defaults.items()}
    outputs = [WORK/name for name in defaults.values()] + [WORK/(name+'_control') for name in defaults.values()]
    outputs += [WORK/'post_resource_01_runtime', WORK/'post_resource_01_driver']
    assert not any(path.exists() or path.is_symlink() for path in outputs)
    proof = load(WORK / 'cumulative_build/binding_proof.json')
    response_record = proof['drivers']['qc_local_response_test']
    assert proof['snapshot_sha256'] == FREEZE and proof['native_makefile_sha256'] == native['makefile_sha256']
    assert sha(WORK/'cumulative_build/qc_local_response_test') == response_record['binary_sha256']
    assert sha(WORK/'cumulative_build/.obj/qc_local_response_test.o') == response_record['object_sha256']
    assert sha(WORK/'cumulative_build/.moc/qc_local_response_test.moc') == response_record['moc_sha256']
    supplementary = tuple(PROGRAM/'tests'/name for name in
        ('qc_local_response_test.cpp','qc_local_response_test.pro','observe_linux_process.py','build_shell_drivers.py'))
    supplementary += tuple(WORK/'cumulative_build'/name for name in
        ('binding_proof.json','verification.json','qc_local_response_test',
         '.obj/qc_local_response_test.o','.moc/qc_local_response_test.moc'))
    executed_inputs = {wsl(HERE/name):sha(HERE/name) for name in scripts}
    executed_inputs.update({wsl(path):sha(path) for path in supplementary})
    plan = {'schema':'sn100-post-platform-preparation-v1','status':'prepared_only_not_executed',
        'source_inputs':447,'source_snapshot_sha256':FREEZE,'native_fresh_objects':210,
        'native_verification_sha256':sha(NATIVE/'verification.json'),'native_makefile_sha256':native['makefile_sha256'],
        'native_artifacts':native['artifacts'],'native_objects_checked_from_actual_files':True,
        'executed_inputs_sha256':executed_inputs,
        'supplementary_inputs_outside_production_freeze':{wsl(path):sha(path) for path in supplementary},
        'old_inputs_preserved':old_hashes,'root_shared_inputs_preserved':preserved_root,
        'exact_commands':commands,'all_output_paths_absent_at_preparation':list(map(wsl,outputs)),
        'response_binary_input':response_record,'response_binding_proof_sha256':sha(WORK/'cumulative_build/binding_proof.json'),
        'AST_parsed':list(scripts),'build_GUI_resource_or_40save_run':False,
        'prerequisites':['Root explicitly grants serial slot after ARM64/other compiler and GUI jobs stop',
                         'Response/resource intervals have no concurrent builds, GUI jobs or bulk hashing',
                         'Failures are retained and retried only with fresh output name'],
        'scope':'Four standalone apps, four extra project/reagent suites, 40 metadata-save response samples, 30s product baseline; no hardware/clinical/RT/long-stability acceptance'}
    save(HERE/'preparation.json',plan)
    assert all(sha(Path(path.replace('/mnt/c/','C:/',1)))==digest for path,digest in old_hashes.items())
    assert all(sha(Path(path.replace('/mnt/c/','C:/',1)))==digest for path,digest in preserved_root.items())
    print(json.dumps({'prepared':True,'scripts_AST':len(scripts),'source_inputs':447,'native_objects_rehashed':210,
                      'outputs_created':False,'controller_sha256':sha(HERE/'execute_stage.py'),
                      'preparation_sha256':sha(HERE/'preparation.json')},indent=2))


if __name__ == '__main__':
    main()
