from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys

assert os.environ.get('WSL_DISTRO_NAME')=='RK3576-Build-22.04'
assert subprocess.check_output(['id','-un']).decode().strip()=='qyq'
sdk=Path('/home/qyq/TaishanPi-3-Linux')
assert sdk.is_dir()
def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(8*1024*1024),b''):value.update(data)
    return value.hexdigest()
files={}
for relative in ['kernel-6.1/.config','kernel-6.1/Makefile','kernel-6.1/include/config/kernel.release','output/.config','ubuntu/chroot-script.sh','device/rockchip/common/post-hooks/zz-rk3576-environment.sh','ubuntu/config/hooks/server/zz-rk3576-environment.sh','ubuntu/overlay/usr/local/sbin/rk3576-first-setup','ubuntu/config/rk3576-ssh-keygen-firstboot.service','rockdev/update.img']:
    path=sdk/relative
    assert path.is_file(),relative
    files[relative]={'bytes':path.stat().st_size,'sha256':digest(path)}
repositories={}
for relative in ['kernel-6.1','ubuntu','device/rockchip','external/rkwifibt','app/ovtree']:
    path=sdk/relative
    repositories[relative]={'head':subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD']).decode().strip(),'status':subprocess.check_output(['git','-C',str(path),'status','--porcelain','--untracked-files=all']).decode()}
prefix=sdk/'prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin'
tools={}
for name,command in {
    'sdk_gcc':[str(prefix/'aarch64-none-linux-gnu-gcc'),'--version'],
    'sdk_gxx':[str(prefix/'aarch64-none-linux-gnu-g++'),'--version'],
    'make':['make','--version'], 'git':['git','--version'],
    'go':['/usr/local/go/bin/go','version'],
    'qemu_aarch64':['/usr/bin/qemu-aarch64-static','--version'],
    'device_tree_compiler':[str(sdk/'kernel-6.1/scripts/dtc/dtc'),'--version'],
}.items():
    result=subprocess.run(command,capture_output=True,text=True)
    assert result.returncode==0,(name,result.stderr)
    tools[name]=(result.stdout or result.stderr).splitlines()[0]
result={'distro':os.environ['WSL_DISTRO_NAME'],'user':subprocess.check_output(['id']).decode().strip(),'home':str(Path.home()),'os_release':Path('/etc/os-release').read_text(),'sdk':str(sdk),'tools':tools,'files':files,'repositories':repositories,'project_access':Path(__file__).resolve().parents[2].is_dir()}
Path(sys.argv[1]).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'user':result['user'],'sdk':str(sdk),'files':len(files),'repositories':len(repositories),'tools':tools},ensure_ascii=False))
