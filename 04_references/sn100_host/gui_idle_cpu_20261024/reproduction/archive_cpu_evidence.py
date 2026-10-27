"""Archive only reproducible sources, logs, counters and untouched screenshots, never runtime databases."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
WORK = ROOT / 'temp/sn100_host/o09_loopback'
OUT = ROOT / '04_references/sn100_host/gui_idle_cpu_20261024'
OUT.mkdir(parents=True, exist_ok=False)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def write(relative, value):
    path = OUT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

copied = []
def copy(source, relative):
    source = Path(source)
    assert source.is_file() and not source.is_symlink()
    assert source.suffix.lower() not in ('.db', '.sqlite', '.secret', '.private', '.exe', '.o', '.so')
    destination = OUT / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    assert sha(source) == sha(destination)
    copied.append({'source': str(source.relative_to(ROOT)), 'archived': relative,
                   'sha256': sha(source), 'bytes': source.stat().st_size})

for name in ('notify_shim.cpp', 'build_notify_shim.sh', 'run_notify_diagnostic.py',
             'material_inputs_before.json', 'diagnostic_summary.json'):
    copy(WORK / 'cpu_before' / name, 'diagnostic_before/' + name)
for mode in ('without_replay', 'smoke_event_filter', 'without_replay_filter', 'with_replay_filter'):
    for source in (WORK / 'cpu_before' / mode).iterdir():
        if source.is_file() and source.suffix in ('.json', '.log', '.png'):
            copy(source, 'diagnostic_before/' + mode + '/' + source.name)
for source in (WORK / 'cpu_after/with_replay_filter').iterdir():
    if source.is_file() and source.suffix in ('.json', '.log', '.png'):
        copy(source, 'diagnostic_after/with_replay_filter/' + source.name)
for name in ('run_notify_diagnostic_after.py', 'compare_pixels.cpp', 'clock_rect.json'):
    copy(WORK / 'cpu_after' / name, 'diagnostic_after/' + name)
copy(WORK / 'cpu_after/pixel_comparison_final.json', 'visual/pixel_comparison.json')
copy(WORK / 'cpu_after/archive_cpu_evidence.py', 'reproduction/archive_cpu_evidence.py')
for name in ('run_cpu_probe.py', 'profile_halo_stack.py', 'build_isolated_cpu.py',
             'build_cpu_candidate.sh', 'build_material_test.sh', 'prepare_native_cache.py'):
    copy(WORK / name, 'reproduction/' + name)
copy(ROOT / '06_programs/sn100_host/tests/observe_linux_process.py', 'reproduction/observe_linux_process.py')
for mode in ('before', 'after'):
    for relative in ('observer.log', 'verification.json', 'process/process.log',
                     'process/samples.json', 'process/summary.json'):
        copy(WORK / 'cpu_comparison' / mode / relative, 'uninstrumented/' + mode + '/' + relative)
for source in (WORK / 'cpu_gdb').iterdir():
    if source.is_file() and source.suffix in ('.log', '.json', '.mi'):
        copy(source, 'gdb/' + source.name)
for name in ('cpu_native_build.log', 'material_before.txt', 'material_candidate_offscreen.txt',
             'material_candidate_xcb.txt'):
    copy(WORK / 'logs' / name, 'logs/' + name)
for mode in ('material_offscreen', 'material_xcb'):
    for name in ('paint_counts.json', 'focused.png'):
        copy(WORK / mode / name, 'tests/' + mode + '/' + name)
for name in ('material_idle_test.cpp', 'material_idle_test.pro'):
    copy(ROOT / '06_programs/sn100_host/tests' / name, 'tests/' + name)
copy(ROOT / '06_programs/sn100_host/tests/fixtures/status_replay.json', 'inputs/status_replay.json')
copy(ROOT / '06_programs/sn100_host/shell/frmmain.ui', 'inputs/frmmain.ui')
copy(WORK / 'cpu_isolated/verification.json', 'build/isolated_verification.json')
copy(WORK / 'cpu_isolated/build.log', 'build/isolated_build.log')

reference = ROOT / '06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/customwidgets/materialUI/qtmaterialflatbutton_internal.cpp'
assert sha(reference) == 'd24f96b9fe12277c78f2387b271dbe815c8774177ae2cc9d136c4b62f13918bb'
copy(reference, 'source/qtmaterialflatbutton_internal.before.cpp')
for name in ('qtmaterialflatbutton_internal.cpp', 'qtmaterialflatbutton_internal.h',
             'qtmaterialflatbutton_p.h', 'qtmaterialflatbutton.cpp', 'qtmaterialflatbutton.h'):
    copy(WORK / 'cpu_isolated/material' / name, 'source/isolated/' + name)
current = ROOT / '06_programs/sn100_host/shell/widgets/material/qtmaterialflatbutton_internal.cpp'
copy(current, 'source/qtmaterialflatbutton_internal.formal_after.cpp')
assert sha(current) == sha(WORK / 'cpu_isolated/material/qtmaterialflatbutton_internal.cpp')

audit = read(WORK / 'cpu_isolated/verification.json')
after_objects = {name: sha(WORK / 'cpu_isolated' / name) for name in audit['object_hashes_before']}
changed = [name for name, value in audit['object_hashes_before'].items() if value != after_objects[name]]
assert len(after_objects) == audit['object_count'] == 124
assert changed == ['.obj/qtmaterialflatbutton_internal.o']
write('build/object_hashes_after.json', {'object_count': len(after_objects),
      'changed_from_baseline': changed, 'sha256_by_object': after_objects,
      'objects_archived': False, 'scope': 'Existing frozen124-object CPU isolation build, only internal object rebuilt'})

before = read(WORK / 'cpu_comparison/before/verification.json')
after = read(WORK / 'cpu_comparison/after/verification.json')
assert before['same_initial_data'] == after['same_initial_data']
assert not before['instrumentation'] and not after['instrumentation']
assert before['exit_code'] == after['exit_code'] == 0
assert before['page'] == after['page'] == 7
before_stats = read(WORK / 'cpu_before/with_replay_filter/notify_435.json')
after_stats = read(WORK / 'cpu_after/with_replay_filter/notify_428.json')
def count_totals(stats):
    return {key: sum(row[key] for row in stats['rows']) for key in
            ('events', 'material_paints', 'zero_halo_opacity_paints', 'zero_halo_size_paints', 'disabled_material_paints')}
before_counts, after_counts = count_totals(before_stats), count_totals(after_stats)
assert before_counts['material_paints'] == 635 and after_counts['material_paints'] == 10
pixel = read(WORK / 'cpu_after/pixel_comparison_final.json')
assert pixel['analysis_excluded_rect_xywh'] == [563, 37, 111, 41]
assert pixel['changed_pixels_outside_rect'] == 0

failures = {
 'schema': 'sn100-gui-idle-cpu-failures-v1',
 'original_notify_interpose': {
    'source': 'This agent tool output and retained diagnostic_before/without_replay/{process.log,run.json}; original wrapper source was overwritten during the temporary shim correction and not separately saved',
    'observation': 'Direct QApplication::notify symbol interposition received no notifications; product exited0 with all workers stopped but no statistics/screenshot was produced, so driver failed its assertion',
    'correction': 'Interpose external QApplication C1/C2 constructors, call RTLD_NEXT original, install public global QObject eventFilter. No vtable change was used'},
 'smoke_navigation': {
    'source': 'This agent tool output plus retained diagnostic_before/smoke_event_filter data',
    'observation': 'Public filter collected808 events in0.1-1 seconds, but driver incorrectly selected the hidden designer button sharing btnDivStatusAdptive name; click was recorded while actual page stayed6 and extra page7 assertion failed',
    'correction': 'Find visible and enabled same-name original dynamic navigation button; complete before/after runs confirm page7'},
 'initial_integrated_build': {
    'source': 'Raw logs/cpu_native_build.log',
    'observation': 'Concurrent instrument integration inputs were not yet present: instrument_widgets.pri could not be read and instrument/instrument_page.h was missing. This build was not used for A/B',
    'correction': 'Use existing frozen124 objects and rebuild only qtmaterialflatbutton_internal.o; preserve exact compile/link command and object hashes'},
 'unmodified_material_red_test': {
    'source': 'Raw logs/material_before.txt',
    'observation': 'Expected defect reproduction:2 passed1 failed because idle600ms produced37 Paint events with invisible halo'},
 'cpu_log_parser_eof': {
    'source': 'Root agent unified-exec tool output transcript/process explanation supplied to this archive agent; no separate original stderr file exists',
    'reported_text': 'Traceback ... run_cpu_probe.py line42 assert marker in log, marker -> AssertionError: "eof"',
    'actual_product_result': 'Existing raw process/summary exit0; final step14 action disconnect; replay.finished and fresh=false finished=true present',
    'correction': 'Only parser assertion changed to replay.finished and --existing added; recomputed same saved raw samples without rerunning before observation',
    'is_original_stderr_file': False}
}
write('failure_note.json', failures)

summary = {
 'schema': 'sn100-gui-idle-cpu-validation-v1',
 'date': '2026-10-24',
 'state': 'WSL x86_64 isolated causal CPU gate passed; not the latest fully integrated instrument-page product',
 'scope': 'Guest, original1600x900 viewport, page7, same anonymous initial data/replay; no reference execution, endpoint or hardware action',
 'mechanism': 'Inherited infinite840ms grow/shrink halo animation calls setHaloScaleFactor each animation tick; old setter requested update unconditionally even when halo transparent or button disabled, provoking repeated header/shadow paints',
 'change': 'Only guard update() for actual visible/enabled button, isHaloVisible and positive haloOpacity/haloSize; always update haloScaleFactor value. Infinite animation timing, focus pulse, ripple/checkable logic, original colors/layout remain',
 'provenance': {'before_main_sha256': audit['baseline_binary_sha256'], 'isolated_after_main_sha256': audit['candidate_binary_sha256'],
                'formal_after_internal_source_sha256': sha(current), 'before_source_matches_readonly_reference_sha256': sha(reference),
                'object_count':124, 'changed_objects':changed, 'artifact_binaries_or_objects_archived':False},
 'independent_material_test': {'before': {'passed':2,'failed':1,'idle_paints_600ms':37,'expected_defect_reproduction':True},
    'offscreen_after': {'passed':3,'failed':0,'counts':read(WORK / 'material_offscreen/paint_counts.json')},
    'xcb_after': {'passed':3,'failed':0,'counts':read(WORK / 'material_xcb/paint_counts.json')},
    'counts_include_init_cleanup':True},
 'full_product_event_diagnostic': {'cpu_baseline':False,'method':'LD_PRELOAD constructor plus public QObject eventFilter;6s visible original navigation click;10-20s event window;20s screenshot;21.5s ordinary SIGTERM',
    'before_with_replay':before_counts,'after_with_replay':after_counts,
    'animation_driver_timer_count_each':625,'main_clock_timer_count_each':10,
    'remaining_paints': '10 after paints with zero halo are concurrent with ordinary1Hz header clock refresh; full product is not claimed to have zero Paint events',
    'all_complete_runs_page':7,'all_complete_runs_exit_code':0,'six_workers_stopped':True,
    'original_runtime_and_binary_hashes_unchanged':True},
 'uninstrumented_resource_comparison': {
    'instrumentation':False,'total_observation_seconds':{'before':before['elapsed_seconds'],'after':after['elapsed_seconds']},
    'steady_definition':'Use existing /proc samples with6<=elapsed_seconds<=20; difference of first/last CPU ticks divided by actual sample span and CLK_TCK; one logical core100%',
    'actual_steady_span_seconds':{'before':before['steady_observed_seconds'],'after':after['steady_observed_seconds']},
    'steady_cpu_percent_one_core':{'before':before['steady_cpu_percent_one_core'],'after':after['steady_cpu_percent_one_core']},
    'whole_run_cpu_percent_includes_startup':{'before':before['cpu_percent_one_logical_core'],'after':after['cpu_percent_one_logical_core']},
    'maximum_RSS_KiB':{'before':before['maximum_VmRSS'],'after':after['maximum_VmRSS']},
    'maximum_PSS_KiB':{'before':before['maximum_Pss'],'after':after['maximum_Pss']},
    'same_initial_data_sha256_map':True, 'window':'About20 seconds total; about14 seconds steady span, not20 steady seconds',
    'limits':'Single paired WSL host observation, no repeated statistical confidence, RK budget, same-board RT or long stability qualification'},
 'visual': {'before_image':'diagnostic_before/with_replay_filter/notify_435.png', 'after_image':'diagnostic_after/with_replay_filter/notify_428.png',
    'original_images_viewed':True,'original_images_never_rewritten':True,'numerical_exclusion_only':'Original lblSystemTime includes both local time/date; parent geometry produces rectangle[563,37,111,41]. No status rectangle excluded because both show replay EOF.',
    'pixel_comparison':pixel,'scope':'One same-page Guest EOF condition; not a complete original Windows visual or all interaction equivalence claim'},
 'gdb':{'scope':'Breakpoint stack only, not a CPU baseline','stack':'gdb/halo_stack.mi','normal_inferior_exit':True},
 'failures':'failure_note.json distinguishes raw evidence from tool-output process transcription',
 'data_boundary':'Only existing closed anonymous runtime four folders copied into temp per run. Database contents, credentials, executables, shared objects and object files are excluded from this archive. Metadata hashes only.',
 'pending':['Latest integrated instrument-page application cumulative and cross-platform freeze by root',
            'Original Windows behavior/activity theme; RK screen/input/resource budget; instrument/long stability/same-board control'],
 'reproduction':'reproduction/ and diagnostic_before/ scripts retain actual environment and commands; recorded old paths are intentional provenance, use new isolated output roots for a fresh authorized rerun'
}
write('validation_summary.json', summary)
write('copied_evidence.json', copied)
write('index.json', {'summary':'validation_summary.json','failures':'failure_note.json',
                    'source_and_object_audit':'build/isolated_verification.json','after_object_hashes':'build/object_hashes_after.json',
                    'hash_manifest':'SHA256SUMS','gui_released':True})
files = sorted(path for path in OUT.rglob('*') if path.is_file())
assert not any(path.suffix.lower() in ('.db','.sqlite','.secret','.private','.exe','.o','.so') for path in files)
(OUT / 'SHA256SUMS').write_text(''.join(sha(path) + '  ' + str(path.relative_to(OUT)) + '\n' for path in files))
print(json.dumps({'archive':str(OUT), 'files':len(files), 'manifest_sha256':sha(OUT / 'SHA256SUMS'),
                  'source_after_sha256':sha(current), 'changed_objects':changed,
                  'pixel_changed_outside_clock':pixel['changed_pixels_outside_rect']}, ensure_ascii=False))
