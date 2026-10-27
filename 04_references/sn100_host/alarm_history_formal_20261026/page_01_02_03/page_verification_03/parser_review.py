"""Read-only replay against02 Makefile; synthetic cases test the recorder only."""
import ast
import importlib.util
import json
import os
from pathlib import Path
import time
import xml.etree.ElementTree as ET

here = Path(__file__).resolve().parent
for name in ('verify_page.py','bounded_process.py'):
    ast.parse((here/name).read_text())
def load(name):
    spec = importlib.util.spec_from_file_location(name,here/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
page = load('verify_page')
helper = load('bounded_process')
out = here/'parser_review'
out.mkdir()
source, source_doc = page.bind_sources()
tools, tool_doc = page.bind_tools()
old_build = here.parent/'page_verification_02/run_01/build'
makefile = page.inspect_makefile(old_build,source_doc,tool_doc,helper)
helper.save(out/'actual02_makefile_binding.json',makefile)
environment = os.environ.copy()
scratch = out/'tmp'
scratch.mkdir()
environment.update(TMPDIR=str(scratch),TMP=str(scratch),TEMP=str(scratch),SQLITE_TMPDIR=str(scratch))
dependency_results = []
for label in ('Qt5Widgets','offscreen','xcb'):
    record = helper.run('ldd_'+label,[tool_doc['tools']['ldd']['path'],tool_doc['tools'][label]['path']],
                        out,here,environment,5,time.monotonic()+6)
    text = (out/('ldd_'+label+'.stdout.log')).read_text()
    page.require(record['exit_code']==0 and 'not found' not in text,'Installed GUI/plugin dependency unavailable')
    dependency_results.append({'label':label,'actual_exit':record['exit_code'],'group_gone':record['owned_group_gone']})
def fixture():
    root = ET.Element('TestCase',{'name':'AlarmHistoryPageTest'})
    for name in ['initTestCase']+page.CASES+['cleanupTestCase']:
        function = ET.SubElement(root,'TestFunction',{'name':name})
        ET.SubElement(function,'Incident',{'type':'pass'})
        if name in page.CASES:
            message = ET.SubElement(function,'Message',{'type':'qinfo'})
            ET.SubElement(message,'Description').text = ('ALARM_HISTORY_PAGE_STORE_CLOSED true WORKER_JOINED true '
                'SQL_ROW_IMAGE_UNCHANGED true NORMAL_PAGE_CLOSE true')
    return root
def accepted(result):
    return result['counts_match'] and result['business_cases']==8 and result['passed']==10 and \
        result['failed']==0 and result['skipped']==0 and result['normal_shutdown_and_SQL_flags']
recorder_cases = []
for kind in ('positive','skip','failure','missing_case','bad_SQL_close_flag'):
    tree = fixture()
    if kind=='skip': tree.find('TestFunction[@name="'+page.CASES[0]+'"]/Incident').set('type','skip')
    if kind=='failure': tree.find('TestFunction[@name="'+page.CASES[0]+'"]/Incident').set('type','fail')
    if kind=='missing_case': tree.remove(tree.find('TestFunction[@name="'+page.CASES[0]+'"]'))
    if kind=='bad_SQL_close_flag':
        node=tree.find('TestFunction[@name="'+page.CASES[0]+'"]/Message/Description')
        node.text=node.text.replace('SQL_ROW_IMAGE_UNCHANGED true','SQL_ROW_IMAGE_UNCHANGED false')
    path=out/('synthetic_recorder_'+kind+'.xml')
    ET.ElementTree(tree).write(path,encoding='utf-8',xml_declaration=True)
    result=page.parse_qt(path)
    expected=kind=='positive'
    page.require(accepted(result)==expected,'Recorder-only parser example failed: '+kind)
    recorder_cases.append({'example':kind,'accepted':accepted(result),'expected':expected})
result={'schema':'sn100-page03-recorder-readonly-review-v1','status':'all_checks_true',
        'source_bound_files':len(source),'installed_tool_bindings':len(tools),
        'actual02_compiled_sources':len(makefile['actual_source_inputs']),
        'actual02_recipe_bindings':makefile['tools'],'dependency_checks':dependency_results,
        'synthetic_recorder_examples':recorder_cases,
        'business_tests_executed':0,'qmake_or_make_or_GUI_executed':False,'SQL_connections_opened':0,
        'note':'Synthetic XML is recorder-unit metadata only; never used as actual Qt/GUI/business evidence.'}
helper.save(here/'parser_review.json',result)
print(json.dumps({'status':result['status'],'actual02_sources':18,
                  'moc_recipes':makefile['tools']['MOC']['recipe_count'],
                  'uic_recipes':makefile['tools']['UIC']['recipe_count'],
                  'rcc_recipes':makefile['tools']['RCC']['recipe_count'],'business_executed':0}))
