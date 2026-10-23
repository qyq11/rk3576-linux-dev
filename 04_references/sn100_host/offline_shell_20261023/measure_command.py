from pathlib import Path
import json, os, resource, subprocess, sys, time

output, stdout_path, *command = sys.argv[1:]
before = resource.getrusage(resource.RUSAGE_CHILDREN)
started = time.monotonic()
samples = []
with open(stdout_path, 'w', encoding='utf-8') as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    proc = Path('/proc') / str(process.pid)
    while process.poll() is None:
        try:
            status = dict(line.split(':', 1) for line in (proc/'status').read_text().splitlines() if ':' in line)
            rollup = dict(line.split(':', 1) for line in (proc/'smaps_rollup').read_text().splitlines() if ':' in line)
            samples.append({'elapsed_ms': (time.monotonic()-started)*1000,
                            'rss_kib': int(status['VmRSS'].split()[0]),
                            'peak_rss_kib': int(status['VmHWM'].split()[0]),
                            'pss_kib': int(rollup['Pss'].split()[0]),
                            'threads': int(status['Threads']),
                            'fds': len(list((proc/'fd').iterdir()))})
        except (FileNotFoundError, ProcessLookupError, KeyError):
            pass
        time.sleep(.2)
elapsed = time.monotonic()-started
after = resource.getrusage(resource.RUSAGE_CHILDREN)
cpu_seconds = (after.ru_utime + after.ru_stime) - (before.ru_utime + before.ru_stime)
result = {'pid': process.pid, 'exit_code': process.returncode, 'elapsed_seconds': elapsed,
          'scope': 'WSL x86_64 Release/XCB; process /proc samples each 200ms; CPU includes any waited-for child, one logical core=100%; not RK acceptance',
          'cpu_seconds': cpu_seconds, 'cpu_percent_one_logical_core': cpu_seconds/elapsed*100,
          'samples': samples}
Path(output).write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k != 'samples'}))
sys.exit(process.returncode)
