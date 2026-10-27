#!/usr/bin/env python3
"""Independently pair existing fresh ARM objects with the frozen source order."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

WORK = Path(__file__).resolve().parent
arm = WORK / 'arm64'
build = arm / 'arm64_build'
rows = [line.split('\t', 1) for line in (arm / 'actual_input_list.tsv').read_text().splitlines()]
sources = [Path(value) for role, value in rows if role == 'source']
headers = [Path(value) for role, value in rows if role == 'header']
mocs = [build / ('moc_' + path.stem + '.cpp') for path in headers if b'Q_OBJECT' in path.read_bytes()]
resources = ['shell_resources', 'account_resources', 'report_resources', 'imagesRpt', 'projects_resources',
             'reagents_resources', 'samples_resources', 'instrument_resources']
generated_resources = [build / ('qrc_' + name + '.cpp') for name in resources]
assert (len(sources), len(mocs), len(generated_resources)) == (75, 57, 8)
expected = [(build / f'obj_{index}.o', path) for index, path in enumerate(sources + mocs + generated_resources)]
helper_sources = [path for role, value in rows if role == 'helper_source'
                  for path in [Path(value)] if path.name in {'report_renderer.cpp', 'report_pdf_mapping.cpp'}]
assert len(helper_sources) == 2
expected += [(build / ('helper_' + path.stem + '.o'), path) for path in helper_sources]
assert len(expected) == 142 and len({path for path, _ in expected}) == 142
start_ns = (WORK / 'arm-before_inventory.json').stat().st_mtime_ns
records = []
for obj, source in expected:
    assert obj.is_file() and obj.stat().st_mtime_ns > start_ns, str(obj)
    result = subprocess.run(['aarch64-linux-gnu-readelf', '-h', '--debug-dump=info', '--dwarf-depth=1', str(obj)],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode == 0 and not result.stderr, (str(obj), result.stderr)
    text = result.stdout
    assert re.search(r'Class:\s+ELF64', text) and re.search(r'Machine:\s+AArch64', text)
    assert re.search(r'Type:\s+REL', text)
    attrs = {}
    for line in text.splitlines():
        for key in ('DW_AT_name', 'DW_AT_comp_dir', 'DW_AT_producer', 'DW_AT_language'):
            if key not in attrs and key in line:
                attrs[key] = line.rsplit(': ', 1)[-1].strip()
    name = Path(attrs['DW_AT_name'])
    if not name.is_absolute():
        name = Path(attrs['DW_AT_comp_dir']) / name
    assert name.resolve() == source.resolve(), (str(obj), attrs, str(source))
    assert '13.3.0' in attrs['DW_AT_producer'] and '-O2' in attrs['DW_AT_producer']
    assert '-g' in attrs['DW_AT_producer'] and '-std=c++14' in attrs['DW_AT_producer']
    records.append({'object': obj.relative_to(arm).as_posix(), 'source': str(source),
                    'object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
                    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                    'object_created_after_prebuild_inventory': True, 'elf': 'ELF64 REL AArch64',
                    'readelf_exit_code': 0, 'dwarf': attrs})
report = {'method': 'Read-only recomputation from existing ELF64 object DWARF compilation units; not a rerun of compilation',
          'formal_source_sequence': 75, 'generated_moc_sequence': 57, 'generated_resource_sequence': 8,
          'main_objects': 140, 'helper_exclusive_objects': 2, 'all_dwarf_sources_verified': True,
          'compiler': 'GNU C++14 13.3.0', 'objects': records}
(arm / 'dwarf_object_sources.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({key: value for key, value in report.items() if key != 'objects'}))
