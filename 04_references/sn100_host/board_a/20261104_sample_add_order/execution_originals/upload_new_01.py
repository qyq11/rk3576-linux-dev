"""固定板A身份上传单份新文件；不覆盖目标，真实输出/退出分别保留。"""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,sys
HERE=Path(__file__).resolve().parent
name,sourcearg,remote=sys.argv[1:]
assert re.fullmatch('[a-z0-9_]+',name) and re.fullmatch('/home/rkshare/programs/sn100_shell/\.board_a_[a-zA-Z0-9_.-]+',remote)
source=Path(sourcearg).resolve(strict=True);assert source.is_file() and not source.is_symlink()
h=hashlib.sha256(source.read_bytes()).hexdigest()
trust=HERE/'known_hosts.board_a'
key=subprocess.run(['ssh-keygen','-E','sha256','-l','-f',str(trust)],capture_output=True)
assert key.returncode==0 and key.stdout.decode().split()[1]=='SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4'
script="set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 3bf8222fe1417f6a ]]\n"+f"target={shlex.quote(remote)}\n[[ ! -e $target && ! -L $target ]]\numask 077\nset -C\ncat >\"$target\"\n[[ $(sha256sum \"$target\" | cut -d' ' -f1) == {h} ]]\nprintf 'UPLOADED_BOARD_A_SHA256=%s\\n' {h}\n"
argv=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','-o','HostKeyAlgorithms=ssh-ed25519',
    '-o','HostKeyAlias=rk3576-board-a','-o','UserKnownHostsFile='+str(trust),'-o','UpdateHostKeys=no','root@192.168.100.123','bash -c '+shlex.quote(script)]
paths={s:HERE/(name+s) for s in ('.command.json','.stdout.log','.stderr.log','.exit')}
assert all(not p.exists() for p in paths.values())
with paths['.command.json'].open('x',encoding='utf-8') as f:json.dump({'argv':argv,'source':str(source),'source_sha256':h,'remote':remote,'board':'board_a'},f,indent=2)
with source.open('rb') as stream:actual=subprocess.run(argv,stdin=stream,capture_output=True)
for s,raw in (('.stdout.log',actual.stdout),('.stderr.log',actual.stderr),('.exit',(str(actual.returncode)+'\n').encode())):
    with paths[s].open('xb') as f:f.write(raw)
sys.stdout.buffer.write(actual.stdout);sys.stderr.buffer.write(actual.stderr)
print('UPLOAD_ACTUAL_EXIT='+str(actual.returncode));raise SystemExit(actual.returncode)
