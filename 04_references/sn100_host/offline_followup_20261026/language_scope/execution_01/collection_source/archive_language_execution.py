#!/usr/bin/env python3
"""Archive completed private language QtCore evidence only; no build, GUI or controller execution."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import traceback
import xml.etree.ElementTree as ET


WORK = Path(__file__).resolve().parents[1]
ROOT = WORK.parents[2]
SOURCE = ROOT / 'temp/sn100_host/offline_followup_20261026/language_scope'
RUN = SOURCE / 'verification_01'
OLD = ROOT / '04_references/sn100_host/offline_followup_20261026/language_scope'
OUT = OLD / 'execution_01'
MANIFEST_SHA = '3e96e34c4ddc54a64d9965be1bc8e73910ac390550edb1f9956b84fc8898e39b'
CONTROLLER_SHA = '5ecd6b2d375efc4afb7cc95343619bbf77a03280d3753573287e166e2cc4d7fd'
OLD_HANDOFF_SHA = 'c3a8f03f3ae22a378d09aa10426ec46958ade75a736ec5dfbbd9999ed16046ba'
STAGES = ('qmake_version', 'qmake_probe', 'make_probe', 'qmake_test', 'make_test', 'test', 'catalog_probe')


def require(ok, why):
    if not ok:
        raise RuntimeError(why)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def text(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT), 'Not ordinary workspace text: ' + str(path))
    raw = path.read_bytes()
    try:
        require(b'\x00' not in raw, 'NUL')
        decoded = raw.decode('utf-8', errors='strict')
    except (UnicodeDecodeError, RuntimeError) as error:
        raise RuntimeError('TEXT_REVIEW_REQUIRED path=' + str(path.relative_to(ROOT))
                           + ' bytes=' + str(len(raw)) + ' sha256=' + sha(raw) + ' error=' + str(error)) from None
    require(not re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', decoded), 'Private key requires review')
    require(not re.search(r'(?i)(?:authorization\s*:\s*bearer\s+|"(?:password_hash|password_salt|access_token|refresh_token)"\s*:\s*"[^"\s]+)', decoded), 'Authentication content requires review')
    return raw


def read(path):
    return json.loads(text(path))


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def add(selected, name, path):
    rel = PurePosixPath(name)
    require(not rel.is_absolute() and '..' not in rel.parts and '\\' not in name
            and not any(x in ('build', 'build_probe', 'build_test', 'runtime', 'test_data', 'tmp', '.obj', '.moc') for x in rel.parts), 'Forbidden archive name')
    require(name not in selected, 'Duplicate archive member')
    selected[name] = path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', required=True, action='store_true')
    parser.add_argument('--attempt', required=True)
    args = parser.parse_args()
    require(args.root_go and re.fullmatch(r'language_archive_\d{2}', args.attempt), 'Explicit go and new attempt required')
    attempt = WORK / 'archive_attempts' / args.attempt
    require(not attempt.exists() and not attempt.is_symlink(), 'Attempt must be fresh')
    attempt.mkdir(parents=True)
    owned = False
    try:
        require(not OUT.exists() and not OUT.is_symlink(), 'Execution archive already exists')
        old_protected = {name:{'bytes':len(text(OLD/name)), 'sha256':sha(text(OLD/name))}
                         for name in ('handoff.json', 'SHA256SUMS')}
        require(old_protected['handoff.json']['sha256'] == OLD_HANDOFF_SHA, 'Existing scope handoff changed; review before archiving')
        manifest = read(SOURCE / 'core_verification_inputs.json')
        require(sha(text(SOURCE / 'core_verification_inputs.json')) == MANIFEST_SHA
                and len(manifest['files']) == 8 and manifest['files']['run_core_verification.py'] == CONTROLLER_SHA,
                'Prepared eight-input identity mismatch')
        execution = read(RUN / 'execution.json')
        require(execution['schema'] == 'sn100-language-core-verification-v1'
                and execution['passed'] is True and execution['controller_return_code'] == 0
                and execution['outer_actual_exit_requires_caller_receipt'] is True
                and execution['slot_released'] is True and execution['cleanup_errors'] == []
                and execution['retries'] == 0 and execution['formal_application_objects_used'] is False
                and execution['gui_started'] is False and execution['system_language_modified'] is False,
                'Completed private-execution identity/scope mismatch')
        require(all(execution[k] is True for k in ('input_and_toolchain_sha_unchanged', 'private_inputs_unchanged_at_exit')),
                'Input stability self-check failed')
        require(execution['input_manifest']['expected_sha256'] == execution['input_manifest']['actual_sha256'] == MANIFEST_SHA
                and read(RUN/'manifest_sha_final.json')['sha256'] == MANIFEST_SHA, 'Manifest execution identity mismatch')
        for prefix in ('input_sha', 'catalog_input', 'toolchain'):
            require(read(RUN/(prefix+'_before.json')) == read(RUN/(prefix+'_after.json')) == read(RUN/(prefix+'_final.json')),
                    prefix + ' changed')
        require(read(RUN/'input_sha_before.json') == manifest['files']
                and read(RUN/'input_manifest_comparison.json') == {'different_paths':[], 'differences':{}}, 'Source map differs')
        selected = {}
        for name,expected in manifest['files'].items():
            require(not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts, 'Input escaped scope')
            require(sha(text(SOURCE/name)) == expected, 'Private input changed: ' + name)
            add(selected, 'private_inputs/' + name, SOURCE/name)
        for name in ('core_verification_inputs.json', 'core_verification_command.json', 'controller_static_review.json'):
            add(selected, 'preparation/' + name, SOURCE/name)
        # Only ordinary top-level receipts are selected. Never enter generated dirs or open the QM.
        top_files = [x for x in RUN.iterdir() if x.is_file()]
        for path in top_files:
            require(path.suffix in ('.json', '.log', '.txt', '.xml', '.exit'), 'Unknown top-level evidence file needs review: ' + path.name)
            add(selected, path.name, path)
        add(selected, 'collection_source/archive_language_execution.py', Path(__file__))
        require([x['name'] for x in execution['stages']] == list(STAGES), 'Missing/extra execution stages')
        for row in execution['stages']:
            name = row['name']; expected_exit = 3 if name == 'catalog_probe' else 0
            command = read(RUN/(name+'.command.json'))
            process = read(RUN/(name+'.process.json'))
            require(read(RUN/(name+'.exit.json')) == row
                    and int(text(RUN/(name+'.exit')).decode('utf-8').strip()) == row['exit_code'] == expected_exit,
                    'Actual stage exit differs: ' + name)
            require(all(command[k] == row[k] for k in command)
                    and process == {'pid':row['pid'], 'pgid':row['pgid'], 'state':'finished',
                                    'exit_code':expected_exit, 'process_group_exists':False}, 'Stage command/process differs')
            require(row['timed_out'] is False and row['state'] == 'finished' and row['error'] is None
                    and row['error_type'] is None and row['cleanup_error'] is None
                    and row['orphaned_children_found'] is False and row['process_group_exists'] is False,
                    'Stage incomplete/cleanup unknown: ' + name)
            require(row['stdout'] == name+'.stdout.log' and row['stderr'] == name+'.stderr.log', 'Raw output paths differ')
            require(all(name+suffix in selected for suffix in ('.command.json','.process.json','.exit.json','.exit','.stdout.log','.stderr.log')),
                    'Missing raw stage evidence')
        groups = execution['owned_process_groups']
        require(len(groups) == len(STAGES) and len({x['pgid'] for x in groups}) == len(STAGES)
                and {x['pgid'] for x in groups} == {x['pgid'] for x in execution['stages']}
                and all(x['exists'] is False for x in groups), 'Owned group closure differs')
        methods = execution['test_totals']['business_methods']
        expected_methods = ['initTestCase'] + methods + ['cleanupTestCase']
        require(len(methods) == len(set(methods)) == 8 and execution['test_totals']['passed'] == 10
                and execution['test_totals']['failed'] == execution['test_totals']['skipped'] == 0, 'Test totals differ')
        qt_text = text(RUN/'qt.txt').decode('utf-8')
        require(re.findall(r'^PASS\s*:\s*LanguageCatalogTest::([^()]+)\(\)',qt_text,re.M) == expected_methods
                and re.search(r'^Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, \d+ms$',qt_text,re.M), 'Raw Qt text differs')
        xml = ET.fromstring(text(RUN/'qt.xml'))
        functions = xml.findall('TestFunction')
        require(xml.tag == 'TestCase' and xml.get('name') == 'LanguageCatalogTest'
                and [x.get('name') for x in functions] == expected_methods
                and all([i.get('type') for i in x.findall('Incident')] == ['pass'] for x in functions)
                and not xml.findall('.//Message'), 'Raw Qt XML differs')
        fixture = read(SOURCE/'fixtures/qt_en_catalog.json')
        declared = bytes.fromhex(fixture['hex'])
        require(len(declared) == fixture['bytes'] == 23 and sha(declared) == fixture['sha256']
                and read(RUN/'catalog_input_before.json')['bytes'] == 23
                and read(RUN/'catalog_input_before.json')['sha256'] == fixture['sha256'], 'Declared 23-byte fixture fingerprint differs')
        observed = read(RUN/'catalog_observed.json')
        require(json.loads(text(RUN/'catalog_probe.stdout.log')) == observed
                and observed['catalog_loaded'] is True and observed['translated_sample_count'] == 0
                and len(observed['samples']) == 2 and all(x['entry_found'] is False and x['translation'] == '' for x in observed['samples'])
                and observed['sample_coverage_complete'] is False and observed['translator_installed'] is False
                and observed['language_setting_written'] is False and observed['full_application_switching_assessed'] is False
                and execution['catalog_actual_exit'] == 3 and execution['catalog_loaded'] is True
                and execution['catalog_application_samples_found'] == 0, 'Negative catalog probe differs')
        before = {name:{'source':path.relative_to(ROOT).as_posix(),'bytes':len(text(path)),'sha256':sha(text(path))}
                  for name,path in sorted(selected.items())}
        OUT.mkdir(); owned = True
        for name,path in sorted(selected.items()):
            value = text(path); item = before[name]
            require(len(value) == item['bytes'] and sha(value) == item['sha256'], 'Source changed before copy')
            dest = OUT/name; dest.parent.mkdir(parents=True,exist_ok=True)
            with dest.open('xb') as handle:handle.write(value)
            require(text(dest) == value, 'Copied bytes differ')
        require({name:{'source':path.relative_to(ROOT).as_posix(),'bytes':len(text(path)),'sha256':sha(text(path))}
                 for name,path in sorted(selected.items())} == before, 'Original inputs changed during archive')
        require({name:{'bytes':len(text(OLD/name)),'sha256':sha(text(OLD/name))} for name in old_protected} == old_protected,
                'Old scope handoff/manifest changed')
        (OUT/'copied_inputs.json').write_bytes(encoded(before))
        parent = {'schema':'sn100-root-parent-tool-observation-v1',
                  'observation_type':'retrospective report from root collaboration message; not an OS/child-emitted parent receipt',
                  'root_exec_session':65663,'actual_parent_exit':0,'tool_completion_chunk':'629d1f',
                  'proof_finished_utc':execution['finished_utc'],'source_of_observation':'Root task authorization message to integration_audit',
                  'os_parent_pid_not_captured':True,'complete_environment_not_reconstructed':True,
                  'parent_full_argv_not_reconstructed':True,'not_independently_reobserved_by_archiver':True}
        (OUT/'root_parent_observation.json').write_bytes(encoded(parent))
        counts = {'private_source_inputs':8,'business_methods':8,'qt_passed':10,'qt_failed':0,'qt_skipped':0,
                  'qt_blacklisted':0,'stages':len(STAGES),'owned_process_groups_gone':len(groups),
                  'catalog_samples':2,'catalog_hits':0,'copied_original_text_files':len(selected),
                  'copied_original_text_bytes':sum(x['bytes'] for x in before.values())}
        proof = {'schema':'sn100-language-execution-metadata-archive-v1','passed':True,
                 'scope':'Private QtCore draft/catalog probe and eight test methods only; not formal language page, whole-app switching, GUI, ARM/RK, clinical or resource measurement.',
                 'counts':counts,'input_manifest_sha256':MANIFEST_SHA,'controller_sha256':CONTROLLER_SHA,
                 'controller_self_reported_return_code':0,'root_parent_observation':parent,
                 'catalog_actual_exit':3,'catalog_loaded':True,'catalog_samples_found':0,
                 'qm_binary_not_read_or_copied_by_archiver':True,'qm_hex_fixture_sha256':fixture['sha256'],
                 'checks':{'original_receipts_crosschecked':True,'source_toolchain_catalog_metadata_before_after_final_equal':True,
                           'copied_original_bytes_sha_and_length_equal':True,'old_scope_handoff_manifest_unchanged':True,
                           'negative_probe_exit_three_preserved':True,'no_business_execution_or_large_artifact_read':True},
                 'old_scope_protected_metadata':old_protected,'complete_environment_not_reconstructed':True,
                 'excluded':['generated build directories/executables/objects','test_data/tmp/runtime/DB/INI','original or reconstructed binary QM','credentials']}
        (OUT/'archive_verification.json').write_bytes(encoded(proof))
        handoff = {'schema':'sn100-language-execution-handoff-v1','status':'completed_private_execution_text_evidence_archived',
                   'archive':OUT.relative_to(ROOT).as_posix(),'source':RUN.relative_to(ROOT).as_posix(),
                   'counts':counts,'execution_record':'execution.json','archive_checks':'archive_verification.json',
                   'copy_index':'copied_inputs.json','hash_manifest':'SHA256SUMS','scope':proof['scope'],
                   'interpretation':'10 passing Qt methods establish the private probe contract. The provided23-byte Qt sidecar loads but matches zero of two application samples, actual probe exit3; this does not establish application English coverage.',
                   'parent_basis':'root_parent_observation.json records root retrospective tool observation, not an independently captured OS parent receipt.',
                   'prepared_command_is_not_actual_parent_argv':'preparation/core_verification_command.json is preserved preparation metadata; its execution_started=false is not rewritten.',
                   'old_scope_files_unchanged':old_protected,'formal_447_objects_used':False,'formal_source_or_controller_changed':False}
        (OUT/'handoff.json').write_bytes(encoded(handoff))
        paths = sorted(x for x in OUT.rglob('*') if x.is_file())
        hashes = {p.relative_to(OUT).as_posix():{'bytes':len(text(p)),'sha256':sha(text(p))} for p in paths}
        (OUT/'SHA256SUMS').write_text(''.join(x['sha256']+'  '+name+'\n' for name,x in hashes.items()),encoding='utf-8',newline='\n')
        require(all(len(text(OUT/name)) == x['bytes'] and sha(text(OUT/name)) == x['sha256'] for name,x in hashes.items()), 'Final manifest reread differs')
        result = {'passed':True,'files':len(hashes)+1,'sha_entries':len(hashes),
                  'handoff_sha256':sha(text(OUT/'handoff.json')),'sha256sums_sha256':sha(text(OUT/'SHA256SUMS')),'counts':counts}
        (attempt/'result.json').write_bytes(encoded(result));print(json.dumps(result,ensure_ascii=False))
    except Exception as error:
        failure = {'passed':False,'error':str(error),'traceback':traceback.format_exc(),'formal_archive_created_by_this_attempt':owned}
        (attempt/'failure.json').write_bytes(encoded(failure))
        if owned:(OUT/'INCOMPLETE.json').write_bytes(encoded(failure))
        raise


if __name__ == '__main__':
    main()
