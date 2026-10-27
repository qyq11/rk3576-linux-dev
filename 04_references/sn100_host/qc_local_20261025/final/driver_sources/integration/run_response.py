"""One serial native QC response observation, with original child receipt."""
import argparse
import math
import os
import re
from common import WORK, PROGRAM, NATIVE, FREEZE, DRIVERS, sha, save, load, require, verify_frozen, verify_native, run_logged

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root-go', action='store_true', required=True)
parser.add_argument('--name', default='response_01')
args = parser.parse_args()
require(args.root_go and os.geteuid() != 0 and re.fullmatch(r'[a-zA-Z0-9_-]+',args.name),
        'Explicit go, ordinary user and simple fresh name required')
out = WORK / args.name
require(not out.exists(), 'Preserve previous response evidence; use a fresh name')
out.mkdir()
frozen = load(FREEZE)
verify_frozen(frozen)
native = verify_native(frozen)
for name, expected in native['artifacts'].items():
    require(sha(NATIVE / 'wsl_release' / name) == expected, 'Native artifact changed: '+name)
proof = load(DRIVERS / 'binding_proof.json')
require(proof['snapshot_sha256']==sha(FREEZE) and
        proof['native_makefile_sha256']==native['makefile_sha256'], 'Response proof uses a different production build')
driver = 'qc_local_response_test'
record = proof['drivers'][driver]
binary = DRIVERS / driver
require(sha(binary) == record['binary_sha256'], 'Response driver changed')
require(sha(PROGRAM / 'tests' / (driver+'.cpp')) == record['source_sha256'], 'Response source changed')
save(out / 'inputs.json', {'source_snapshot_sha256':sha(FREEZE),'driver':record,
     'main_artifacts':native['artifacts'],'condition':'All development, builds, other GUI jobs and heavy hashing stopped; one original-page observation'})
env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
           SN100_WSL_IME='off', SN100_QC_EVIDENCE=str(out / 'evidence'))
save(out / 'environment.json', {k:env[k] for k in ('QT_QPA_PLATFORM','QT_SCALE_FACTOR',
     'QT_AUTO_SCREEN_SCALE_FACTOR','SN100_WSL_IME','SN100_QC_EVIDENCE')})
receipt = run_logged('response',[str(binary),'-v1','-o',str(out/'qt.txt')+',txt',
                    '-o',str(out/'qt.xml')+',junitxml'],out,env,120)
metrics = load(out/'evidence/metrics.json')
require(metrics['test_function_passed'] and metrics['normal_page_leave'] and metrics['qc_worker_closed']
        and metrics['catalog_worker_closed'], 'Normal response shutdown not proven')
require(metrics['executable_sha256'] == sha(binary), 'Metrics binary mismatch')
require(len(metrics['samples']) == 40 and metrics['heartbeats'], 'Wrong sample count')
def summary(values):
    ordered=sorted(values)
    return {'count':len(values),'p95_ms':ordered[math.ceil(.95*len(values))-1],
            'p99_ms':ordered[math.ceil(.99*len(values))-1],'max_ms':ordered[-1]}
checks = {}
for field, start, end in [('queued_to_busy','queued_ns','first_busy_ns'),
                          ('queued_to_finished','queued_ns','operation_finished_ns'),
                          ('queued_to_committed_page','queued_ns','committed_page_observed_ns')]:
    values=[]
    for row in metrics['samples']:
        require(row['success'] and row['input_name']==row['page_name'] and
                row['input_manufacturer']==row['page_manufacturer'], 'Saved response fields differ')
        value=(row[end]-row[start])/1e6
        require(value>=0 and math.isclose(value,row[field+'_ms'],rel_tol=1e-9,abs_tol=1e-9), 'Raw response arithmetic differs')
        values.append(value)
    calculated=summary(values)
    require(all(math.isclose(v,metrics[field][k],rel_tol=1e-9,abs_tol=1e-9) for k,v in calculated.items()), 'Response percentile differs')
    checks[field]=calculated
previous_observed = None
for row in metrics['heartbeats']:
    planned, observed = row['planned_ns'], row['observed_ns']
    previous = planned - 5_000_000
    require(previous_observed is None or previous == previous_observed, 'Heartbeat baseline differs from prior dispatch')
    require(observed >= previous and row['interval_ns'] == observed - previous and
            row['delay_ns'] == max(0, observed - planned), 'Raw heartbeat timing arithmetic differs')
    previous_observed = observed
for field, raw in [('heartbeat_delay','delay_ns'),('heartbeat_interval','interval_ns')]:
    values=[row[raw]/1e6 for row in metrics['heartbeats']]
    calculated=summary(values)
    require(all(math.isclose(v,metrics[field][k],rel_tol=1e-9,abs_tol=1e-9) for k,v in calculated.items()), 'Heartbeat percentile differs')
    checks[field]=calculated
verify_frozen(frozen)
save(out/'verification.json',{'passed':True,'actual_exit':receipt['exit_code'],
     'metrics_sha256':sha(out/'evidence/metrics.json'),'arithmetic':checks,'sample_count':40,
     'heartbeat_raw_relations_verified':True,
     'scope':'Native original QC page short response baseline only; physical input/paint completion, approved budget, Windows original, RK/instrument and long stability pending'})
print(checks,flush=True)
