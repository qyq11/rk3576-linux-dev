"""Read source/layout/catalog facts only; do not load Qt or execute the reference app."""
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent / 'audit_02'
WORK.mkdir(exist_ok=False)
REF = ROOT / '06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')

sources = ['frmuserset/frmlanguagesetting.cpp', 'frmuserset/frmlanguagesetting.h',
    'frmuserset/frmlanguagesetting.ui', 'frmuserset/frmuserset.cpp', 'frmuserset/frmuserset.h',
    'frmuserset/frmuserset.ui', 'frmuserset/frmuserset.pri', 'main.cpp', 'SeeRain450.pro',
    'main.qrc', 'public/stylecolor.cpp', 'public/stylecolor.h', 'frmmain/frmmain.cpp']
records = [{'path': path, 'bytes': (REF / path).stat().st_size, 'sha256': sha(REF / path)} for path in sources]
scans = []
queries = [
    ('translator_api', ['-g', '*.{cpp,h}'], r'QTranslator|installTranslator|removeTranslator'),
    ('language_change', ['-g', '*.{cpp,h}'], r'LanguageChange'),
    ('catalog_build_resource', ['-g', '*.{pro,pri,qrc}'], 'TRANSLATIONS|[.]qm([^A-Za-z0-9_]|$)|[.]ts([^A-Za-z0-9_]|$)'),
    ('configuration_key', ['-i', '-g', '*.{cpp,h}', '-g', '!**/include/**',
      '-g', '!**/thirdparty/**', '-g', '!**/resource/Keyboard/**'],
      '(^|[^A-Za-z0-9_])(value|setValue|beginGroup|readEntry|writeEntry)[ ]*[(].*(language|locale|语言)'),
    ('language_callers', ['-g', '*.{cpp,h,pri}'], r'frmLanguageSetting|frmlanguagesetting|on_btnLanguage_clicked')]
for name, options, pattern in queries:
    command = ['rg', '-n', '--hidden', '--no-ignore', '-g', '!**/bin/**'] + options + [pattern, str(REF)]
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
    if result.returncode not in (0, 1) or result.stderr:
        raise RuntimeError('Read-only source search failed: ' + name + '\n' + result.stderr)
    (WORK / (name + '.txt')).write_text(result.stdout, encoding='utf-8')
    scans.append({'name': name, 'command': command, 'actual_exit': result.returncode,
                  'matches': len(result.stdout.splitlines()), 'output': name + '.txt'})
inventory_cmd = ['rg', '--files', '--hidden', '--no-ignore', str(REF)]
inventory = subprocess.run(inventory_cmd, capture_output=True, text=True, encoding='utf-8')
if inventory.returncode:
    raise RuntimeError('Reference filename inventory failed')
catalogs = []
for line in inventory.stdout.splitlines():
    path = Path(line)
    if path.suffix.lower() in ('.ts', '.qm'):
        catalogs.append({'path': path.relative_to(REF).as_posix(), 'bytes': path.stat().st_size})
catalogs.sort(key=lambda row: row['path'])
tree = ET.parse(REF / 'frmuserset/frmlanguagesetting.ui')
widgets = []
for item in tree.findall('.//widget'):
    row = {'name': item.attrib.get('name'), 'class': item.attrib.get('class'), 'properties': {}}
    for prop in item.findall('property'):
        if prop.attrib['name'] == 'geometry':
            row['properties']['geometry'] = {x.tag: int(x.text) for x in prop.find('rect')}
        elif prop.attrib['name'] in ('text', 'styleSheet', 'readOnly', 'focusPolicy', 'windowTitle'):
            row['properties'][prop.attrib['name']] = ''.join(prop.itertext()).strip()
        elif prop.attrib['name'] == 'font':
            row['properties']['font'] = {x.tag: x.text for x in prop.find('font')}
    widgets.append(row)
layout = {'schema': 'sn100-language-reference-layout-v1', 'widgets': widgets,
          'explicit_connections': len(tree.findall('./connections/connection')),
          'embedded_resources': len(tree.findall('./resources/include')),
          'runtime_style_sources': {'list': 'listWidgetStyle::getStrSheet1(), stylecolor.cpp:143',
              'list_selection_color': 'main.cpp:153 <- theme/kListWidgetRowSelected',
              'apply_normal_hover_pressed': 'frmUserSet stack buttonStyle, main.cpp:157 <- theme/kBtnNormalColor,kBtnHoverColor,kBtnPressedColor',
              'right_menu': 'frmUserSet theme/rightBaseColor,rightBtnColor,rightBtnFontColor; white alpha 0..25 gradient'},
          'limits': 'Source geometry/style contract only. No original Windows active theme, DPI, font or visual runtime checked; no old configuration values read.'}
qm = REF / 'bin/translations/qt_en.qm'
fixture = {'source': qm.relative_to(REF).as_posix(), 'bytes': qm.stat().st_size,
           'sha256': sha(qm), 'hex': qm.read_bytes().hex(),
           'scope': 'Exact provided Qt English sidecar bytes, not an application English translation proof'}
(WORK / 'fixtures').mkdir(exist_ok=True)
save(WORK / 'fixtures/qt_en_catalog.json', fixture)
save(WORK / 'layout_contract.json', layout)
save(WORK / 'read_only_sources.json', {'reference_root': str(REF.relative_to(ROOT)), 'files': records})
save(WORK / 'translation_resource_inventory.json', {'filename_command': inventory_cmd,
    'actual_exit': inventory.returncode, 'catalogs': catalogs,
    'ts_files': sum(row['path'].endswith('.ts') for row in catalogs),
    'qt_en_fixture': fixture, 'QTranslator_load_executed': False})
save(WORK / 'search_receipts.json', scans)
after = [{'path': row['path'], 'sha256': sha(REF / row['path']),
          'unchanged': sha(REF / row['path']) == row['sha256']} for row in records]
if not all(row['unchanged'] for row in after):
    raise RuntimeError('Reference source changed during audit')
save(WORK / 'reference_after_read.json', after)
print(json.dumps({'read_only_source_files': len(records), 'scans': scans,
                  'catalog_files': len(catalogs), 'ts_files': sum(row['path'].endswith('.ts') for row in catalogs),
                  'english_sidecar_bytes': fixture['bytes'], 'reference_unchanged': True,
                  'Qt_build_gui_or_reference_execution': False}, ensure_ascii=True))
