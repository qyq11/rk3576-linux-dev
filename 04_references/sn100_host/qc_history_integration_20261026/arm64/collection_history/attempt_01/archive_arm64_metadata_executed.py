#!/usr/bin/env python3
"""Archive completed447 ARM text receipts; never execute tools or open build/runtime/sysroot files."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import traceback


WORK = Path(__file__).resolve().parents[1]
ROOT = WORK.parents[2]
ARM = WORK / 'platforms/arm64'
NATIVE = WORK / 'native'
OUT = ROOT / '04_references/sn100_host/qc_history_integration_20261026/arm64'
PREFIX = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'
FREEZE = '1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'
PREPARATION = '91e0d6a83a8cf092db09988be3991788a047d2b9377ec9047de6d9027d070dc2'
PARENT_OBSERVATION = 'edd26d08c6741694e4c0acae7eb08e04df7939282527c9f938aba359b8777b8d'
FIXED_STEPS = {'compiler_version', 'compiler_banner_version', 'linker_version', 'moc_version',
               'uic_version', 'rcc_version', 'snapshot_before', 'list_inputs', 'build',
               'snapshot_after_build', 'closure'}
TOP = ('verification.json', 'started.json', 'prebuild_inputs.json', 'steps.json',
       'actual_commands.log', 'actual_input_list.tsv', 'compiler_receipt_summary.json',
       'dwarf_object_sources.json', 'link_source_map.json', 'source_before.json',
       'source_after_build.json', 'sysroot_before.json', 'sysroot_after.json',
       'toolchain_before.json', 'toolchain_after.json', 'controller_inputs_before.json',
       'controller_inputs_after.json', 'native_binding_before.json', 'native_binding_after.json',
       'platform_preparation_input.json', 'capture_compiler.py', 'trace_build.bash',
       'root_parent_observation.json')


def require(ok, why):
    if not ok:
        raise RuntimeError(why)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def ordinary(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink()
            and path.resolve(strict=True).is_relative_to(ROOT), 'Not an ordinary workspace text: ' + str(path))
    data = path.read_bytes()
    try:
        require(b'\x00' not in data, 'NUL')
        text = data.decode('utf-8', errors='strict')
    except (UnicodeDecodeError, RuntimeError) as error:
        raise RuntimeError('TEXT_REVIEW_REQUIRED path=' + str(path.relative_to(ROOT))
                           + ' bytes=' + str(len(data)) + ' sha256=' + sha(data)
                           + ' reason=' + str(error)) from None
    require(not re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', text),
            'Private-key content requires review: ' + str(path))
    require(not re.search(r'(?i)(?:authorization\s*:\s*bearer\s+|"(?:password_hash|password_salt|access_token|refresh_token)"\s*:\s*"[^"\s]+)', text),
            'Authentication content requires review: ' + str(path))
    return data


def read(path):
    return json.loads(ordinary(path).decode('utf-8'))


def linux(path):
    return PREFIX + path.relative_to(ROOT).as_posix()


def relative(name):
    p = PurePosixPath(name)
    require(not p.is_absolute() and '..' not in p.parts and '\\' not in name,
            'Unsafe relative metadata path: ' + name)
    return p


def add(selected, name, source):
    p = relative(name)
    require(not any(x in ('build', 'runtime', 'sysroot', '.obj', '.moc', 'secret', 'secrets') for x in p.parts),
            'Forbidden copied namespace: ' + name)
    require(name not in selected, 'Duplicate selected path: ' + name)
    selected[name] = source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--attempt', required=True)
    args = parser.parse_args()
    require(args.root_go and re.fullmatch(r'arm64_archive_\d{2}', args.attempt), 'Explicit go and unique attempt required')
    attempt = WORK / 'archive_attempts' / args.attempt
    require(not attempt.exists() and not attempt.is_symlink(), 'Attempt must be new')
    attempt.mkdir(parents=True)
    owned_output = False
    try:
        require(not OUT.exists() and not OUT.is_symlink(), 'Formal archive must be new, never overwrite')
        require(OUT.resolve().is_relative_to((ROOT / '04_references/sn100_host/qc_history_integration_20261026').resolve()), 'Output escaped evidence root')
        v = read(ARM / 'verification.json')
        frozen = read(NATIVE / 'final_before.json')
        nv = read(NATIVE / 'verification.json')
        require(v['schema'] == 'sn100-qc-history-arm64-build-offline-v1'
                and v['source_entries'] == len(frozen) == 447 and v['source_freeze_sha256'] == FREEZE
                and sha(ordinary(NATIVE / 'final_before.json')) == FREEZE
                and v['build_exit_code'] == v['closure_exit_code'] == 0
                and v['packaging_performed'] is False and v['closure_errors'] == []
                and v['preparation_sha256'] == PREPARATION, 'ARM identity or completed exits mismatch')
        require(all(v[k] is True for k in ('source_before_after_equal_native_freeze',
                'sysroot_fingerprint_unchanged', 'full_object_recompilation',
                'toolchain_fingerprints_unchanged', 'exact_per_invocation_receipts'))
                and v['board_runtime_verified'] is False and v['real_printer_or_instrument_used'] is False,
                'ARM proof state or scope mismatch')
        require(read(ARM / 'source_before.json') == read(ARM / 'source_after_build.json') == frozen,
                'Source maps disagree')
        for name in ('sysroot', 'toolchain', 'controller_inputs', 'native_binding'):
            require(read(ARM / (name + '_before.json')) == read(ARM / (name + '_after.json')),
                    name + ' metadata before/after differs')
        require(nv['passed'] is True and nv['actual_exit'] == 0 and nv['source_inputs'] == 447
                and nv['snapshot_sha256'] == FREEZE and nv['source_before_after_equal'] is True
                and sha(ordinary(NATIVE / 'verification.json')) == v['native_verification_sha256'], 'Native binding mismatch')
        binding = read(ARM / 'native_binding_before.json')
        require(binding == {'verification_sha256': v['native_verification_sha256'],
                           'snapshot_sha256': FREEZE, 'source_entries': 447}, 'Native receipt binding mismatch')
        prep = read(ARM / 'platform_preparation_input.json')
        require(sha(ordinary(ARM / 'platform_preparation_input.json')) == PREPARATION
                and prep['freeze']['sha256'] == FREEZE and prep['freeze']['entries'] == 447,
                'Copied preparation identity differs')
        controls = read(ARM / 'controller_inputs_before.json')
        require(controls == prep['controller_inputs'], 'Copied prep/controller roster differs')
        selected = {}
        for name in TOP:
            add(selected, name, ARM / name)
        for row in controls:
            name = row['path']; relative(name)
            require(name.startswith('integration_drivers/'), 'Controller input escaped controller root')
            path = WORK / name; raw = ordinary(path)
            require(len(raw) == row['bytes'] and sha(raw) == row['sha256'], 'Current controller differs: ' + name)
            add(selected, 'controller_inputs/' + name, path)
        require(ordinary(ARM / 'capture_compiler.py') == ordinary(WORK / 'integration_drivers/arm64/capture_compiler.py'),
                'Executed capture source differs from bound controller')
        for name in ('verification.json', 'final_before.json'):
            add(selected, 'native_binding/' + name, NATIVE / name)
        for name in ('build.sh', 'check_arm64.py'):
            path = ROOT / '06_programs/sn100_host/shell' / name
            rel = path.relative_to(ROOT / '06_programs/sn100_host').as_posix()
            require(sha(ordinary(path)) == frozen[rel], 'Formal build/check source differs')
            add(selected, 'producer_source/' + name, path)
        add(selected, 'collection_source/archive_arm64_metadata.py', Path(__file__))

        steps = read(ARM / 'steps.json')
        names = [x['name'] for x in steps]
        require(len(names) == len(set(names)), 'Repeated step names')
        objects = read(ARM / 'dwarf_object_sources.json')
        rows = objects['objects']; count = len(rows)
        require(count > 0 and count == objects['total_objects'] == v['object_summary']['total_objects'], 'Object count differs')
        require({k:objects[k] for k in v['object_summary']} == v['object_summary'], 'Object summary differs')
        expected_steps = FIXED_STEPS | {'object_' + str(i) + '_readelf' for i in range(count)}
        require(set(names) == expected_steps, 'Missing/extra step receipts')
        for row in steps:
            name = row['name']
            exit_row = read(ARM / (name + '_exit.json'))
            command = read(ARM / (name + '_command.json'))
            process = read(ARM / (name + '_process.json'))
            require(exit_row == row and row['exit_code'] == 0 and row['timed_out'] is False
                    and row['state'] == 'finished' and row['owned_process_group_stopped'] is True
                    and row['cleanup_errors'] == [], 'Incomplete/failed step: ' + name)
            require(all(command[k] == row[k] for k in command)
                    and process == {'pid':row['pid'], 'state':'finished', 'exit_code':0}, 'Command/process mismatch: ' + name)
            for suffix in ('_command.json', '_exit.json', '_process.json', '.log'):
                add(selected, name + suffix, ARM / (name + suffix))

        receipts = {}
        for path in sorted((ARM / 'compiler_receipts').iterdir()):
            require(path.is_file() and path.suffix == '.json', 'Unexpected compiler receipt member')
            name = path.relative_to(ARM).as_posix(); row = read(path)
            require(row['exit_code'] == 0 and row['finished_ns'] >= row['started_ns']
                    and re.fullmatch(r'[0-9a-f]{64}', row['output_sha256']) and row['output_bytes'] > 0,
                    'Failed compiler invocation: ' + name)
            receipts[name] = row; add(selected, name, path)
        compiles = {k:x for k,x in receipts.items() if '-c' in x['argv']}
        links = {k:x for k,x in receipts.items() if '-c' not in x['argv']}
        summary = read(ARM / 'compiler_receipt_summary.json')
        require(len(compiles) == count == summary['compile_invocations']
                and len(links) == summary['link_invocations'] == 2
                and len(receipts) == summary['total_invocations']
                and summary['all_exit_codes_zero'] is True, 'Compiler invocation inventory differs')
        require(len({x['object'] for x in rows}) == count
                and {x['compiler_receipt'] for x in rows} == set(compiles), 'Object/compiler bijection differs')
        formal_sources = 0
        by_object = {}
        for i, row in enumerate(rows):
            receipt = compiles[row['compiler_receipt']]
            require(row['created_after_prebuild'] is True and row['readelf_exit_code'] == 0
                    and row['readelf_receipt'] == 'object_' + str(i) + '_readelf_exit.json'
                    and row['elf'] == 'ELF64 REL AArch64'
                    and row['actual_compile_argv'] == receipt['argv']
                    and receipt['source'] == row['source']
                    and receipt['output'] == linux(ARM / row['object'])
                    and receipt['output_sha256'] == row['object_sha256'], 'Object metadata mismatch: ' + str(i))
            source_prefix = PREFIX + '06_programs/sn100_host/'
            if row['source'].startswith(source_prefix):
                require(frozen[row['source'][len(source_prefix):]] == row['source_sha256'], 'Formal compile source SHA mismatch')
                formal_sources += 1
            by_object[receipt['output']] = row
        linkmap = read(ARM / 'link_source_map.json')
        require(linkmap['paired_fresh_main_and_renderer'] is True
                and {x['role'] for x in linkmap['links']} == {'main', 'renderer'}
                and {x['compiler_receipt'] for x in linkmap['links']} == set(links), 'Paired links differ')
        link_sets = {}
        for row in linkmap['links']:
            receipt = links[row['compiler_receipt']]
            expected_hash = v['main_sha256'] if row['role'] == 'main' else v['renderer_sha256']
            require(row['output_sha256'] == receipt['output_sha256'] == expected_hash
                    and row['output'] == receipt['output'], 'Linked output receipt differs')
            linked = [x for x in receipt['argv'] if x.endswith('.o')]
            require(linked == [x['object'] for x in row['objects']]
                    and len(linked) == len(set(linked)), 'Actual linked object order differs')
            for obj in row['objects']:
                source = by_object[obj['object']]
                require(obj['object_sha256'] == source['object_sha256']
                        and obj['source'] == source['source'], 'Link source mapping differs')
            link_sets[row['role']] = set(linked)
        require(len(link_sets['main']) == objects['main_objects']
                and len(link_sets['renderer'] - link_sets['main']) == objects['helper_exclusive_objects']
                and link_sets['main'] | link_sets['renderer'] == set(by_object), 'Main/helper object partition differs')

        closure = read(ARM / 'dependencies/dependency_report.json')
        files = closure['files']
        require(closure['offline_link_and_sql_closure_passed'] is True and closure['errors'] == []
                and closure['board_runtime_verified'] is False and len(files) == v['closure_elf_count']
                and len({x['path'] for x in files}) == len(files)
                and all(x['class'] == 'ELF64' and x['machine'] == 'AArch64' for x in files), 'Closure receipt differs')
        binaries = {role:x for x in files for role in x['roles'] if role in ('main', 'report_renderer')}
        require(binaries['main']['sha256'] == v['main_sha256']
                and binaries['report_renderer']['sha256'] == v['renderer_sha256'], 'Closure/main/helper SHA differs')
        sums = ordinary(ARM / 'dependencies/SHA256SUMS').decode('utf-8').splitlines()
        require(set(sums) == {x['sha256'] + '  ' + x['path'] for x in files}, 'Closure SHA list differs')
        for name in ('dependency_report.json', 'dependency_summary.txt', 'SHA256SUMS'):
            add(selected, 'dependencies/' + name, ARM / 'dependencies' / name)
        parent = read(ARM / 'root_parent_observation.json')
        require(sha(ordinary(ARM / 'root_parent_observation.json')) == PARENT_OBSERVATION
                and parent['actual_parent_exit'] == 0 and parent['root_exec_session'] == 15298
                and parent['tool_completion_chunk'] == 'ca4cd8'
                and parent['os_parent_pid_not_captured'] is True
                and parent['complete_environment_not_reconstructed'] is True
                and parent['observation_type'].startswith('retrospective'), 'Root retrospective observation differs')
        before = {name:{'source':path.relative_to(ROOT).as_posix(), 'bytes':len(ordinary(path)),
                        'sha256':sha(ordinary(path))} for name,path in sorted(selected.items())}
        # All source selection and ordinary-text gates precede the first formal archive write.
        OUT.mkdir(parents=True); owned_output = True
        for name,path in sorted(selected.items()):
            raw = ordinary(path); item = before[name]
            require(len(raw) == item['bytes'] and sha(raw) == item['sha256'], 'Input changed before copy: ' + name)
            dest = OUT / name; dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open('xb') as handle:
                handle.write(raw)
            copied = ordinary(dest)
            require(len(copied) == item['bytes'] and sha(copied) == item['sha256'], 'Copied input differs: ' + name)
        after = {name:{'source':path.relative_to(ROOT).as_posix(), 'bytes':len(ordinary(path)),
                       'sha256':sha(ordinary(path))} for name,path in sorted(selected.items())}
        require(after == before, 'Inputs changed during archive')
        (OUT / 'copied_inputs.json').write_bytes(encoded(before))
        counts = {'copied_original_text_files':len(selected), 'copied_original_bytes':sum(x['bytes'] for x in before.values()),
                  'source_inputs':len(frozen), 'objects_metadata_only':count, 'formal_compile_units':formal_sources,
                  'main_objects':len(link_sets['main']), 'renderer_linked_objects':len(link_sets['renderer']),
                  'helper_exclusive_objects':len(link_sets['renderer'] - link_sets['main']),
                  'compiler_invocations':len(receipts), 'compile_invocations':len(compiles), 'link_invocations':len(links),
                  'steps':len(steps), 'closure_elf_count':len(files)}
        checks = {'original_text_utf8_no_nul':True, 'source_native_controller_metadata_binding_verified':True,
                  'each_original_step_exit_command_process_verified':True, 'compiler_object_link_metadata_verified':True,
                  'closure_metadata_and_original_sums_verified':True, 'copied_bytes_sha256_readback_equal':True,
                  'selected_original_inputs_unchanged':True, 'no_object_executable_or_sysroot_content_read':True,
                  'no_build_test_gui_probe_or_packaging_executed':True}
        verification = {'schema':'sn100-arm64-metadata-archive-v1', 'passed':True,
                        'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        'source_freeze_sha256':FREEZE, 'preparation_sha256':PREPARATION,
                        'native_verification_sha256':v['native_verification_sha256'],
                        'arm_verification_sha256':before['verification.json']['sha256'],
                        'main_sha256_from_original_proofs':v['main_sha256'],
                        'renderer_sha256_from_original_proofs':v['renderer_sha256'],
                        'counts':counts, 'checks':checks, 'root_parent_observation':parent,
                        'scope':'Completed ARM cross-build and offline dependency proof metadata only; no RK execution, instrument, clinical, response/resource or stability claim.',
                        'complete_environment_not_reconstructed':True, 'artifact_content_rehashed':False,
                        'excluded':['build tree and all objects/executables', 'sysroot content', 'runtime databases and INI',
                                    'credentials/authentication materials', 'recovery preparations and recovery output']}
        (OUT / 'collection_verification.json').write_bytes(encoded(verification))
        handoff = {'schema':'sn100-arm64-metadata-handoff-v1', 'status':'archived_original_text_receipts',
                   'source':ARM.relative_to(ROOT).as_posix(), 'archive':OUT.relative_to(ROOT).as_posix(),
                   'source_freeze_sha256':FREEZE, 'preparation_sha256':PREPARATION, 'counts':counts,
                   'proof':'verification.json', 'archive_checks':'collection_verification.json',
                   'exact_copy_index':'copied_inputs.json', 'sha256_manifest':'SHA256SUMS',
                   'actual_arm_parent_exit':0,
                   'parent_basis':'root_parent_observation.json is retrospective tool completion evidence, not an independent OS-emitted parent receipt; no parent PID or full environment invented.',
                   'scope':verification['scope'], 'no_original_source_or_evidence_modified':True}
        (OUT / 'handoff.json').write_bytes(encoded(handoff))
        manifest = {p.relative_to(OUT).as_posix():{'bytes':len(ordinary(p)), 'sha256':sha(ordinary(p))}
                    for p in sorted(OUT.rglob('*')) if p.is_file()}
        (OUT / 'SHA256SUMS').write_text(''.join(x['sha256'] + '  ' + name + '\n' for name,x in manifest.items()), encoding='utf-8', newline='\n')
        require(all(len(ordinary(OUT/name)) == x['bytes'] and sha(ordinary(OUT/name)) == x['sha256'] for name,x in manifest.items()), 'Final SHA readback differs')
        result = {'passed':True, 'archive':str(OUT), 'counts':counts, 'manifest_entries':len(manifest),
                  'manifest_sha256':sha(ordinary(OUT/'SHA256SUMS')), 'handoff_sha256':sha(ordinary(OUT/'handoff.json'))}
        (attempt / 'result.json').write_bytes(encoded(result))
        print(json.dumps(result, ensure_ascii=False))
    except Exception as error:
        failure = {'passed':False, 'error':str(error), 'traceback':traceback.format_exc(),
                   'formal_archive_created_by_this_attempt':owned_output, 'no_business_stage_rerun':True}
        (attempt / 'failure.json').write_bytes(encoded(failure))
        if owned_output:
            (OUT/'INCOMPLETE.json').write_bytes(encoded(failure))
        raise


if __name__ == '__main__':
    main()
