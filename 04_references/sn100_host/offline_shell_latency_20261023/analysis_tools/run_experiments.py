from pathlib import Path
import json, os, subprocess, sys, time

task = Path(__file__).resolve().parent
root = task.parents[2]
attempt = task/'attempts/final_03'
runner = root/'06_programs/sn100_host/tests/shell_latency.sh'
conditions = [
    ('burst','burstNavigation',{}),
    ('paced','pacedNavigation',{}),
    ('normal','normalOperations',{}),
    ('overhead','evidenceOverhead',{}),
    ('capture_burst','burstNavigation',{'SN100_LATENCY_CAPTURE_FIRST':'1'}),
    ('proc_burst','burstNavigation',{'SN100_LATENCY_PROC_SAMPLER':'1'}),
]
manifest = attempt/'runs_manifest.json'
assert not manifest.exists(), 'Never overwrite an experiment series'
assert (attempt/'build_metadata.json').is_file()
rows=[]
def save():
    temporary=manifest.with_suffix('.new')
    temporary.write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    temporary.replace(manifest)
def sample(pid, started):
    proc=Path('/proc')/str(pid)
    status=dict(line.split(':',1) for line in (proc/'status').read_text().splitlines() if ':' in line)
    rollup=dict(line.split(':',1) for line in (proc/'smaps_rollup').read_text().splitlines() if ':' in line)
    return {'elapsed_ms':(time.monotonic()-started)*1000,
        'image':(proc/'comm').read_text().strip(),
        'rss_kib':int(status['VmRSS'].split()[0]),'peak_rss_kib':int(status['VmHWM'].split()[0]),
        'pss_kib':int(rollup['Pss'].split()[0]),'threads':int(status['Threads']),
        'fds':len(list((proc/'fd').iterdir()))}
for repetition in range(1,4):
    # Round-robin mixes conditions instead of giving all warm runs to one mode.
    for name,slot,flags in conditions:
        evidence=attempt/'runs'/f'{name}_{repetition:02d}'
        log=attempt/'logs'/f'{name}_{repetition:02d}.log'
        assert not evidence.exists() and not log.exists()
        env=dict(os.environ)
        for key in ['SN100_LATENCY_CAPTURE_FIRST','SN100_LATENCY_PROC_SAMPLER']:
            env.pop(key,None)
        env.update(flags)
        env['SN100_LATENCY_WORK_DIR']=str(attempt)
        env['SN100_LATENCY_EVIDENCE_DIR']=str(evidence)
        started=time.monotonic()
        samples=[]
        print(f'START {name} round={repetition}',flush=True)
        with log.open('x',encoding='utf-8') as output:
            process=subprocess.Popen(['bash',str(runner),'run',slot],env=env,stdout=output,stderr=subprocess.STDOUT)
            while process.poll() is None:
                if time.monotonic()-started > 100:
                    process.kill()
                    process.wait()
                    raise RuntimeError('Only this experiment process exceeded 100 seconds: '+name)
                try:
                    samples.append(sample(process.pid,started))
                except (OSError,KeyError,ValueError,ProcessLookupError):
                    pass
                time.sleep(.2)
        result=evidence/(slot+'_results.json')
        row={'condition':name,'round':repetition,'slot':slot,'evidence_dir':str(evidence),
            'result_path':str(result),'log_path':str(log),'exit_code':process.returncode,
            'elapsed_seconds':time.monotonic()-started,'flags':flags,
            'observer':'Separate Python process samples /proc every 200ms; never read in GUI timer.',
            'external_resource_samples':samples}
        rows.append(row)
        save()
        print(f'END {name} round={repetition} exit={process.returncode} elapsed={row["elapsed_seconds"]:.3f}s',flush=True)
        if process.returncode or not result.is_file():
            raise RuntimeError('Stop at first failed experiment; inspect '+str(log))
print('All 18 fresh-process experiments completed.',flush=True)
