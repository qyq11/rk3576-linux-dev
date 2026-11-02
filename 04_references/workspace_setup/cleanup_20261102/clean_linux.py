from pathlib import Path
import hashlib,json,os,subprocess

here=Path(__file__).resolve().parent
plan=json.loads((here/'linux_plan.json').read_text())
root=Path(plan['root']).resolve()
assert root==Path('/home/qyq/TaishanPi-3-Linux')
assert os.environ.get('WSL_DISTRO_NAME')=='RK3576-Build-22.04'
assert json.loads((here/'archive_verification.json').read_text())['failed']==0
active=set(subprocess.check_output(['ps','-eo','comm=']).decode().split())
assert not active.intersection({'make','cc1','cc1plus','gcc','g++','ld','ccache','go','sn100_shell'})
sources=[root/'kernel-6.1',root/'external/rkwifibt']
def statuses():return {str(path):subprocess.check_output(['git','-C',str(path),'status','--porcelain','--untracked-files=no']).decode() for path in sources}
before=statuses()
config_before=hashlib.sha256((root/'kernel-6.1/.config').read_bytes()).hexdigest()
deleted=0;byte_count=0
for entry in plan['files']:
    path=Path(entry['path'])
    assert not path.is_symlink() and path.is_file() and path.resolve().is_relative_to(root)
    assert path.stat().st_size==entry['bytes']
    path.unlink();deleted+=1;byte_count+=entry['bytes']
go='/usr/local/go/bin/go'
cache=Path(subprocess.check_output([go,'env','GOCACHE']).decode().strip()).resolve()
assert cache==Path('/home/qyq/.cache/go-build')
cache_files=[path for path in cache.rglob('*') if path.is_file()]
cache_bytes=sum(path.stat().st_size for path in cache_files)
subprocess.run([go,'clean','-cache'],check=True)
subprocess.run(['sudo','apt-get','clean'],check=True,capture_output=True)
assert statuses()==before
assert hashlib.sha256((root/'kernel-6.1/.config').read_bytes()).hexdigest()==config_before
assert (root/'rockdev/update.img').stat().st_size==3032177283
assert (root/'ubuntu/ubuntu-rootfs.img').is_file()
remaining=sum(Path(entry['path']).exists() for entry in plan['files'])
result={'date':'2026-11-02','deleted':deleted,'bytes':byte_count,'remaining':remaining,'go_cache_before_files':len(cache_files),'go_cache_before_bytes':cache_bytes,'apt_cache_cleaned':True,'tracked_source_status_unchanged':True,'kernel_config_sha256':config_before,'final_sdk_image_retained':True,'prepared_rootfs_retained':True}
(here/'linux_result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
assert remaining==0
