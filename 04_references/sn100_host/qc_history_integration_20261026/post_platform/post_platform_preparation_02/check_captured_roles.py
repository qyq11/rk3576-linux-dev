"""Read-only captured-Makefile role checks; no compiler, executable, or GUI is launched."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from build_roles import RoleError, parse_graph, verify_role

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def wsl(path):
    text = Path(path).as_posix()
    return '/mnt/c/' + text.split(':/', 1)[1] if ':/' in text else text


def main():
    captures = [(WORK/'post_standalone_01/build'/name/'Makefile', 'failed447_attempt_compile_graph_only')
                for name in ('quality_control','calibration')]
    old = ROOT/'temp/sn100_host/qc_local_20261025/projects_modules_final'
    captures += [(old/name/'build/Makefile','historical426_topology_fixture_only_no_current_build_claim')
                 for name in ('projects_app','project_basis','project_advanced','reagents_app','reagent_basis','reagent_batches')]
    results, graphs = [], {}
    for path, scope in captures:
        before = sha(path)
        graph = parse_graph(path.read_text(encoding='utf-8'), wsl(PROGRAM), wsl(path.parent))
        proof = verify_role(graph)
        graphs[graph['target']] = graph
        results.append({'case':'positive:'+graph['target'],'passed':True,'captured_makefile':str(path),
                        'sha256':before,'fixture_scope':scope,'role_proof':proof})
        assert sha(path) == before

    def must_fail(label, graph, message):
        try:
            verify_role(graph)
        except RoleError as error:
            assert message in str(error), (label, str(error), message)
            results.append({'case':label,'passed':True,'expected_rejection':str(error)})
            return
        raise AssertionError('Missing required business source was accepted: '+label)

    for target, graph in graphs.items():
        required = verify_role(graph)['required_business_sources']
        for source in required:
            changed = deepcopy(graph)
            changed['declared_sources'].remove(source)
            must_fail('missing_declared:'+target+':'+source,changed,'missing_declared_business_source')
            changed = deepcopy(graph)
            changed['object_sources'] = {obj:actual for obj,actual in changed['object_sources'].items() if actual != source}
            must_fail('missing_linked_object:'+target+':'+source,changed,'missing_compiled_business_source')
    for actual, wrong in (('sn100_qc_local','sn100_calibration'),('sn100_calibration','sn100_qc_local')):
        changed = deepcopy(graphs[actual])
        changed['target'] = wrong
        must_fail('wrong_role:'+actual+'->'+wrong,changed,'missing_declared_business_source')
    changed = deepcopy(graphs['sn100_calibration'])
    changed['target'] = 'sn100_unknown'
    must_fail('unknown_target',changed,'unknown_target_role')
    # QC still contains ProjectsStore in this real capture; that must not mask missing history core.
    qc = graphs['sn100_qc_local']
    assert 'projects/projects_store.cpp' in qc['declared_sources']
    assert 'projects/projects_store.cpp' not in verify_role(qc)['required_business_sources']
    assert 'projects/projects_store.cpp' not in graphs['sn100_calibration']['declared_sources']
    assert not verify_role(graphs['sn100_calibration'])['ProjectsStore_requirement']
    for target in ('sn100_projects','projects_test','project_advanced_test','sn100_reagents','test_reagents','reagent_batch_test'):
        assert verify_role(graphs[target])['ProjectsStore_requirement']
        assert any(row['case']=='missing_declared:'+target+':projects/projects_store.cpp' for row in results)
    summary = {'schema':'sn100-captured-makefile-role-checks-v1','passed':True,
        'classifier_sha256':sha(HERE/'build_roles.py'),'checker_sha256':sha(Path(__file__)),
        'positive_captured_graphs':len(captures),'negative_cases':len(results)-len(captures),'cases':results,
        'old_build_outputs_reused_for_execution':False,'runtime_or_compile_acceptance_claimed':False,
        'calibration_accepts_its_real_graph_without_ProjectsStore':True,
        'QC_requires_history_core_page_SQL_and_presenter':True,
        'ProjectsStore_required_only_for_project_reagent_roles':True,
        'source_and_object_mapping_mutations_only_in_memory':True}
    (HERE/'role_checks.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:summary[key] for key in ('passed','positive_captured_graphs','negative_cases',
                                                'runtime_or_compile_acceptance_claimed')},indent=2))


if __name__ == '__main__':
    main()
