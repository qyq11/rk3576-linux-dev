"""Positive business-source contracts for actual standalone qmake target roles."""
import posixpath
import re
import shlex


class RoleError(RuntimeError):
    pass


QC = ('quality_control/main.cpp', 'quality_control/qc_store.cpp',
      'quality_control/qc_history_core.cpp', 'quality/statistics.cpp',
      'quality_control/quality_control_page.cpp', 'quality_control/qc_history_presenter.cpp',
      'quality_control/cus_quality_chart.cpp', 'quality_control/qualitychart.cpp')
CALIBRATION = ('calibration/main.cpp', 'calibration/calibration_store.cpp',
               'calibration/calibration_page.cpp', 'calibration/calibrationchart.cpp')
PROJECT = ('projects/projects_store.cpp', 'projects/project_page.cpp', 'projects/project_advanced.cpp')
REAGENT = ('projects/projects_store.cpp', 'projects/project_advanced.cpp',
           'reagents/reagent_store.cpp', 'reagents/reagents_page.cpp')
ROLES = {'sn100_qc_local': QC, 'sn100_calibration': CALIBRATION,
         'sn100_projects': PROJECT + ('projects/main.cpp',),
         'projects_test': PROJECT + ('projects/tests/projects_test.cpp',),
         'project_advanced_test': PROJECT + ('projects/tests/project_advanced_test.cpp',),
         'sn100_reagents': REAGENT + ('reagents/main.cpp',),
         'test_reagents': REAGENT + ('reagents/tests/test_reagents.cpp',),
         'reagent_batch_test': REAGENT + ('reagents/tests/reagent_batch_test.cpp',)}


def parse_graph(text, program, build):
    text = text.replace('\\\n', ' ')
    program, build = program.rstrip('/'), build.rstrip('/')

    def words(name):
        rows = re.findall(r'^' + name + r'\s*=\s*(.*)$', text, re.M)
        if len(rows) != 1:
            raise RoleError('makefile_variable_missing_or_ambiguous:' + name)
        return shlex.split(rows[0])

    def source_name(word):
        path = posixpath.normpath(word if posixpath.isabs(word) else build + '/' + word)
        if path.startswith(program + '/'):
            return path[len(program) + 1:]
        if path.startswith(build + '/'):
            return path[len(build) + 1:]
        raise RoleError('external_compile_source:' + path)

    targets = words('TARGET')
    if len(targets) != 1:
        raise RoleError('one_target_required')
    objects = words('OBJECTS')
    if not objects or len(objects) != len(set(objects)):
        raise RoleError('missing_or_duplicate_objects')
    declared = [source_name(word) for word in words('SOURCES')]
    rules = {}
    for obj, source in re.findall(r'^([^\s:]+\.o):\s+([^\s]+\.cpp)(?:\s|$)', text, re.M):
        if obj in rules:
            raise RoleError('ambiguous_object_compile_rule:' + obj)
        rules[obj] = source_name(source)
    missing = sorted(set(objects) - set(rules))
    if missing:
        raise RoleError('object_without_compile_rule:' + repr(missing))
    return {'target': targets[0], 'declared_sources': declared, 'ordered_objects': objects,
            'object_sources': {obj: rules[obj] for obj in objects}}


def verify_role(graph):
    target = graph['target']
    if target not in ROLES:
        raise RoleError('unknown_target_role:' + target)
    required = set(ROLES[target])
    declared = set(graph['declared_sources'])
    missing = sorted(required - declared)
    if missing:
        raise RoleError('missing_declared_business_source:' + target + ':' + repr(missing))
    linked_objects = {obj: source for obj, source in graph['object_sources'].items()
                      if obj in graph['ordered_objects']}
    compiled = set(linked_objects.values())
    missing = sorted(required - compiled)
    if missing:
        raise RoleError('missing_compiled_business_source:' + target + ':' + repr(missing))
    if declared != compiled:
        raise RoleError('declared_sources_and_linked_compile_rules_differ:' + target)
    bindings = {source: [obj for obj, actual in linked_objects.items() if actual == source]
                for source in sorted(required)}
    if any(len(objects) != 1 for objects in bindings.values()):
        raise RoleError('ambiguous_required_source_object:' + target)
    return {'target': target, 'required_business_sources': sorted(required),
            'required_source_object_bindings': bindings, 'object_count': len(graph['ordered_objects']),
            'source_count': len(declared), 'all_declared_sources_have_linked_compile_rule': True,
            'ProjectsStore_requirement': target in ('sn100_projects','projects_test','project_advanced_test',
                'sn100_reagents','test_reagents','reagent_batch_test')}
