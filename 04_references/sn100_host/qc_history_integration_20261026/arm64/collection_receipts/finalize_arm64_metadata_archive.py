#!/usr/bin/env python3
"""Append the actual successful metadata-collector receipts and reread the complete new archive."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'temp/sn100_host/qc_history_integration_20261026'
OUT = ROOT / '04_references/sn100_host/qc_history_integration_20261026/arm64'


def require(ok, why):
    if not ok:
        raise RuntimeError(why)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def raw(path):
    require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT), str(path))
    value = path.read_bytes()
    require(b'\0' not in value, 'NUL requires root review: ' + str(path))
    value.decode('utf-8', errors='strict')
    return value


def read(path):
    return json.loads(raw(path))


def dump(path, value, exclusive=False):
    with path.open('xb' if exclusive else 'wb') as handle:
        handle.write((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def main():
    require(not (OUT / 'INCOMPLETE.json').exists(), 'Incomplete archive')
    collected = read(OUT / 'copied_inputs.json')
    for name, item in collected.items():
        require(len(raw(OUT / name)) == item['bytes'] and sha(raw(OUT / name)) == item['sha256'], 'Original copied bytes differ')
        require(len(raw(ROOT / item['source'])) == item['bytes']
                and sha(raw(ROOT / item['source'])) == item['sha256'], 'Original source changed')
    dest = OUT / 'collection_receipts'
    dest.mkdir()
    new_inputs = {}
    sources = [WORK / 'integration_drivers' / ('arm64_archive_collection_02' + suffix)
               for suffix in ('.command.json', '.process.json', '.exit.json', '.stdout.log', '.stderr.log')]
    sources.append(WORK / 'archive_attempts/arm64_archive_02/result.json')
    sources.append(Path(__file__).resolve())
    receipt = read(WORK / 'integration_drivers/arm64_archive_collection_02.exit.json')
    require(receipt['exit_code'] == 0 and receipt['timed_out'] is False, 'Actual metadata collector did not exit0')
    for source in sources:
        value = raw(source); path = dest / source.name
        with path.open('xb') as handle:
            handle.write(value)
        require(raw(path) == value, 'Receipt reread differs')
        new_inputs[path.relative_to(OUT).as_posix()] = {'source':source.relative_to(ROOT).as_posix(),
                                                     'bytes':len(value), 'sha256':sha(value)}
    argv = ['git', 'check-ignore', '-v', '--', OUT.relative_to(ROOT).as_posix() + '/handoff.json']
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    proc = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    dump(dest / 'git_ignore_command.json', {'argv':argv, 'cwd':str(ROOT), 'started_utc':started}, True)
    # Save the actual return before checking its expected semantics.
    dump(dest / 'git_ignore_exit.json', {'exit_code':proc.returncode,
         'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
         'expected_exit_for_not_ignored':1}, True)
    (dest / 'git_ignore.stdout.log').write_bytes(proc.stdout)
    (dest / 'git_ignore.stderr.log').write_bytes(proc.stderr)
    raw(dest / 'git_ignore.stdout.log'); raw(dest / 'git_ignore.stderr.log')
    require(proc.returncode == 1 and not proc.stdout and not proc.stderr, 'New archive is ignored or git check failed')
    dump(OUT / 'collection_receipt_inputs.json', new_inputs, True)
    verification = read(OUT / 'collection_verification.json')
    verification['actual_metadata_collector_exit'] = receipt
    verification['collection_receipts'] = 'collection_receipts'
    verification['checks']['actual_metadata_collector_exit_zero'] = True
    verification['checks']['git_not_ignored_actual_exit_one'] = True
    verification['checks']['copied_originals_reread_at_finalization'] = True
    dump(OUT / 'collection_verification.json', verification)
    handoff = read(OUT / 'handoff.json')
    handoff['collector_actual_exit'] = 0
    handoff['collector_receipt'] = 'collection_receipts/arm64_archive_collection_02.exit.json'
    handoff['collector_first_attempt_failed_before_formal_copy'] = True
    handoff['collector_failure_history'] = 'collection_history/attempt_01'
    handoff['collector_failure_reason'] = 'Collector initially omitted two real metadata self-hash rows from the closure SHA list; original 56 ELF rows were present. No ARM build/test/probe was rerun.'
    handoff['git_ignore_actual_exit'] = 1
    dump(OUT / 'handoff.json', handoff)
    # SHA256SUMS has no self-entry. Every other original/derived byte is reread.
    paths = sorted(x for x in OUT.rglob('*') if x.is_file() and x != OUT / 'SHA256SUMS')
    require(all(not any(p in ('build', 'runtime', 'sysroot', '.obj', '.moc') for p in x.relative_to(OUT).parts)
                for x in paths), 'Forbidden copied directory')
    entries = {p.relative_to(OUT).as_posix():{'bytes':len(raw(p)), 'sha256':sha(raw(p))} for p in paths}
    (OUT / 'SHA256SUMS').write_text(''.join(v['sha256'] + '  ' + name + '\n' for name,v in entries.items()), encoding='utf-8', newline='\n')
    require(all(len(raw(OUT/name)) == v['bytes'] and sha(raw(OUT/name)) == v['sha256'] for name,v in entries.items()), 'Final reread differs')
    for name,item in collected.items():
        require(sha(raw(ROOT/item['source'])) == item['sha256'], 'Original input changed at finalization')
    for name,item in new_inputs.items():
        require(sha(raw(ROOT/item['source'])) == item['sha256'], 'Collector receipt input changed')
    result = {'passed':True, 'files':len(entries)+1, 'sha_entries':len(entries),
              'copied_arm_originals_and_provenance':len(collected),
              'copied_collection_receipt_inputs':len(new_inputs),
              'sha256sums_sha256':sha(raw(OUT/'SHA256SUMS')),
              'handoff_sha256':sha(raw(OUT/'handoff.json')),
              'collection_verification_sha256':sha(raw(OUT/'collection_verification.json')),
              'arm_verification_sha256':sha(raw(OUT/'verification.json')),
              'copied_inputs_sha256':sha(raw(OUT/'copied_inputs.json')),
              'collector_exit':receipt['exit_code'], 'root_parent_exit':read(OUT/'root_parent_observation.json')['actual_parent_exit']}
    dump(WORK / 'archive_attempts/arm64_archive_02/final_archive_readback.json', result, True)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
