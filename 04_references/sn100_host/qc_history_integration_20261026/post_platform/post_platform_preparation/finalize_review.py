"""Read-only QA and draft metadata. Does not import or execute the prepared runners."""
import ast
import difflib
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
OLD = ROOT / 'temp/sn100_host/qc_local_20261025/integration_drivers'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def save(name, value):
    (HERE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def win(path):
    assert path.startswith('/mnt/c/')
    return Path('C:/' + path[len('/mnt/c/'):])


def main():
    plan = load(HERE / 'preparation.json')
    frozen = load(WORK / 'native/final_before.json')
    assert all(sha(win(path)) == digest for path, digest in plan['executed_inputs_sha256'].items())
    assert all(sha(win(path)) == digest for path, digest in plan['old_inputs_preserved'].items())
    assert all(sha(win(path)) == digest for path, digest in plan['root_shared_inputs_preserved'].items())
    assert all(not win(path).exists() for path in plan['all_output_paths_absent_at_preparation'])
    entries = ('quality_control/quality_control.pro', 'calibration/calibration.pro',
        'projects/tests/project_advanced_test.pro','projects/tests/projects_test.pro',
        'reagents/tests/reagent_batch_test.pro','reagents/tests/tests.pro','projects/projects.pro','reagents/reagents.pro')
    graph, unresolved = {}, []
    for entry in entries:
        pending, seen = [PROGRAM/entry], set()
        while pending:
            path = pending.pop().resolve()
            if path in seen:
                continue
            seen.add(path)
            name = path.relative_to(PROGRAM).as_posix()
            assert name in frozen and path.is_file() and sha(path) == frozen[name]
            children = []
            for raw in re.findall(r'^\s*include\s*\(\s*([^\)]+)\s*\)', path.read_text(encoding='utf-8-sig'), re.M):
                value = raw.strip().replace('$$PWD', path.parent.as_posix())
                if '$$' in value:
                    unresolved.append({'owner':name,'reference':raw})
                    continue
                child = Path(value)
                if not child.is_absolute():
                    child = path.parent/child
                child = child.resolve()
                assert child.is_relative_to(PROGRAM.resolve()) and child.is_file()
                children.append(child.relative_to(PROGRAM).as_posix())
                pending.append(child)
            graph[name] = {'sha256':sha(path),'include':children}
    assert not unresolved
    save('input_dependencies.json', {'entry_projects':entries,'project_include_graph':graph,
        'unresolved_includes':unresolved,'all_project_inputs_in_current447':True,
        'supplementary_root_tests_and_existing_driver_artifacts':plan['supplementary_inputs_outside_production_freeze'],
        'native':{'objects':210,'main_and_helper':plan['native_artifacts'],
                  'makefile_sha256':plan['native_makefile_sha256']},
        'future_fresh_build_proof':'Actual Makefile SOURCES/OBJECTS, generated artifacts, timestamps and binary hashes checked after each qmake/make; not claimed during preparation'})
    findings = [
        {'old':'common.py::WORK/NATIVE/DRIVERS','finding':'直接执行旧路径会读取qc_local_20261025的旧native和累计测试产物。',
         'adaptation':'仅复制到新目录；同一相对路径计算现在指向qc_history_integration_20261026，且硬门槛447/1486686b、210鲜编对象。'},
        {'old':'run_projects_modules.py:29-30,325','finding':'写死426/5123bf14冻结；ProjectsStore的旧固定SHA恰好仍与当前447一致，不能单靠该文件SHA宣称新集成通过。',
         'adaptation':'使用当前447冻结身份，同时保存原测试源码、驱动、主辅程序、Makefile与对象的首尾绑定。'},
        {'old':'run_standalone.py:181','finding':'旧质控启动提示已被新历史页面接入后的文案取代，会误报未加载。',
         'adaptation':'按当前quality_control_page.cpp的真实元数据加载提示核对；不冒充历史曲线加载完成。'},
        {'old':'run_response.py:30','finding':'绑定旧累计构建；且根tests中的响应源码/监测脚本不在447生产冻结中。',
         'adaptation':'绑定新累计构建的响应binary/moc/object/proof及额外根tests文件，逐一存SHA；不重编响应程序。'},
        {'old':'tests/qc_local_response_test.cpp:99','finding':'QTemporaryDir建在可执行文件旁，直接用共享cumulative_build中的程序会在那里写临时运行库。',
         'adaptation':'把已核字节的测试程序复制到本次新输出/executable后执行，保持共享累计目录不写。'},
        {'old':'run_response.py module scope','finding':'没有main保护，导入即可解析参数并进入执行。',
         'adaptation':'仅AST审阅；控制器以独立进程执行，准备阶段未导入或运行它。'},
        {'old':'standalone/response/resource environment','finding':'继承环境清理不完整，可能带入旧SN100、Qt、QMake或交叉工具链变量。',
         'adaptation':'清除相关变量并使用新的XDG/TMP目录；只写本阶段显式匿名参数。'},
        {'old':'standalone/projects/response completion','finding':'旧通用完成路径没有一致的最终进程组消失门槛；仅退出直接子进程不足以推定清场。',
         'adaptation':'每个新会话登记PID/PGID及Linux启动标识；真实退出先落盘，控制器核对全部组消失。超时仅处理本轮可识别组；身份不明或残留明确失败并交根处理。'},
        {'old':'common.py::stop_owned_group','finding':'旧清理中直接子进程提前退出会缩短后代进程的TERM宽限。',
         'adaptation':'新副本按整个进程组等待后才升级KILL；清理不能制造子进程成功退出结论。'},
        {'old':'run_standalone.py module build inventory','finding':'旧独立程序只登记binary/Makefile SHA，未逐项保留私有对象和生成文件。',
         'adaptation':'复用已审阅的verify_build/build_inventory核对实际源、对象、生成文件和首尾一致。'},
        {'old':'run_resource.py::steady_window','finding':'10至30秒是观察器时钟固定窗口，未证明10秒前已就绪；VmHWM为进程全生命周期峰值。',
         'adaptation':'保留正确口径和原始/proc采样，不改称稳态峰值、严格100ms采样或性能预算通过。'}]
    save('review.json', {'status':'reviewed_and_adapted_in_new_temp_only','findings':findings,
        'current_QC_thread_topology':'历史查询复用QcStore与原质控数据库线程；没有新增第十个SQL工作线程。原九个SQL关闭标记及两个INI关闭标记仍按当前shell/main.cpp核对。',
        'no_prior426_or432_result_reclassified':True,'no_source_shared_controller_public_doc_or_old_evidence_edited':True})
    stages = [
        {'stage':'standalone','command':plan['exact_commands']['standalone'],
         'inputs':['当前447源/210对象/主辅程序','quality_control.pro及calibration.pro的递归pri','新匿名运行目录'],
         'actions':'两个新Release独立构建；质控与校准各一次xcb启动并1800ms正常关闭；另一次当前主程序只初始化校准所需匿名catalog，等SHELL_READY后正常SIGTERM退出。',
         'expected':'7个实际阶段退出0；QC新元数据提示、校准加载与正常关闭标记、catalog所有者关闭/线程结束；私有对象及源/native首尾相同。',
         'owned_group_count':7},
        {'stage':'projects','command':plan['exact_commands']['projects'],
         'inputs':['四套当前项目/试剂测试cpp/pro','项目与试剂独立pro及递归pri','当前447/210绑定','新匿名catalog'],
         'actions':'6个新Release构建；四套完整回归offscreen，项目与试剂各一次xcb独立启动、1800ms正常关闭。',
         'expected':'项目高级11/0、项目基础9/0、试剂批组11/0、试剂基础11/0，均含初始化/清理；两个app真实退出0及加载证据。',
         'owned_group_count':18},
        {'stage':'response','command':plan['exact_commands']['response'],
         'inputs':['新累计构建中已绑定447/210的qc_local_response_test及binary/moc/object/proof',
                   '额外绑定的根tests响应cpp/pro和新隔离可执行副本'],
         'actions':'xcb真实原保存按钮40次；5ms心跳；保留原始时钟、字段与分位数重算；不在测量中做批量hash。',
         'expected':'Qt3/0，40条成功且字段一致；真实退出0、正常离页、QC/catalog worker关闭，响应及心跳计算逐条匹配。',
         'owned_group_count':1,'limit':'只测质控资料保存，不测有历史数据的曲线/查询负载。'},
        {'stage':'resource','command':plan['exact_commands']['resource'],
         'inputs':['当前新主辅程序、447冻结、210鲜编对象','额外绑定的observe_linux_process.py','新匿名运行根及一次性引导输入'],
         'actions':'xcb1600×900、Guest入口、九SQL加两INI、本地127.0.0.1 Qj回环；30秒普通SIGTERM，观察器50秒内限、外层65秒限。',
         'expected':'实际观察器及主程序均退出0，summary匹配，九SQL/两INI各关闭一次、THREADS_STOPPED、回环真实报文可解析；组已消失后才只读表行数和文件大小。',
         'owned_group_count':1,'limit':'10<=t<30固定观察时钟窗口；不作RK、实时控制、临床效力或长稳结论，也不等价于有历史点的质控负载。'}]
    save('execution_plan.json', {'status':'prepared_only_awaiting_root_slot','order':stages,
        'four_independent_apps':['质控','校准','项目/单位','试剂'],
        'root_scheduling_requirement':'先等ARM64及其他编译/GUI结束；每阶段单独授槽。项目/独立构建清场后才安排响应和资源，测量期间其他窗口不编译、不运行GUI、不批量hash。',
        'real_exit_and_cleanup':'每阶段先落真实command/process/log/exit，再解析结果；外层stage实际退出独立保留。成功需所有登记PGID已消失且无需强杀；超时或失败保留本目录，重试必须另名。无法确认退出或清场时不得开始下一阶段。',
        'authentication_input_scope':'仅本轮随机引导文件；不读/hash/归档其内容，正常finally删除；失败控制器仅按已登记命令中的受限临时路径清理。',
        'host_WSL_exit_requirement':'根执行外层wsl.exe时另保存原始宿主退出码，不从Python验证字段或组消失推断。',
        'not_executed':True})
    commands = '\n\n'.join(stage+'\n'+' '.join(command) for stage,command in plan['exact_commands'].items())
    (HERE/'RUN_COMMANDS.txt').write_text('Prepared only. Root must grant one serial slot per command; all output paths are currently absent.\n\n'+commands+'\n',encoding='utf-8')
    for name in ('common.py','run_standalone.py','run_projects_modules.py','run_response.py','run_resource.py'):
        old = (OLD/name).read_text(encoding='utf-8').splitlines(True)
        new = (HERE/name).read_text(encoding='utf-8').splitlines(True)
        (HERE/(name+'.patch')).write_text(''.join(difflib.unified_diff(old,new,fromfile='old/'+name,tofile='new/'+name)),encoding='utf-8')
    save('preparation_attempts.json', {'preparation_only':True,
        'initial_readonly_generator_check':{'actual_exit':1,'tool_chunk':'734347',
            'reason':'Windows inventory keys used backslashes; same447 file hashes, no actual source change.',
            'correction':'Normalize only the Windows read-only key view to forward slashes; freeze and06 sources untouched.'},
        'subsequent_generator_and_AST_checks':{'actual_exit':0,'last_tool_chunk':'c99f12'},
        'compiler_GUI_resource_or_response_invocations':0})
    save('preparation_qa.json', {'status':'static_preparation_checks_passed','AST_files':list(plan['AST_parsed']),
        'eight_qmake_entries_and_recursive_includes_resolved':True,'qmake_graph_files':len(graph),
        'source447_and_native210_actual_hash_checks_passed':True,'extra_unfrozen_inputs_bound':len(plan['supplementary_inputs_outside_production_freeze']),
        'all_ten_execution_output_paths_absent':True,'old_and_root_shared_inputs_unchanged':True,
        'runtime_controller_or_prepared_driver_executed':False,'runtime_execution_still_required':True})
    files = {path.name:sha(path) for path in sorted(HERE.iterdir()) if path.is_file() and path.name!='files_sha256.json'}
    save('files_sha256.json', files)
    print(json.dumps({'status':'prepared_only','qmake_graph_files':len(graph),'supplementary_inputs':len(plan['supplementary_inputs_outside_production_freeze']),
                      'files':len(files),'preparation_sha256':sha(HERE/'preparation.json'),
                      'execution_output_dirs_created':False,'06_or_old_shared_inputs_modified':False},indent=2))


if __name__ == '__main__':
    main()
