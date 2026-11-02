from pathlib import Path
import json, os, subprocess

sdk=Path('/home/qyq/TaishanPi-3-Linux').resolve()
assert os.environ.get('WSL_DISTRO_NAME')=='RK3576-Build-22.04'
assert sdk==Path('/home/qyq/TaishanPi-3-Linux')
items=[]
for relative in ['kernel-6.1','external/rkwifibt']:
    directory=sdk/relative
    tracked=set(subprocess.check_output(['git','-C',str(directory),'ls-files','-z']).decode().split('\0'))
    for current,dirs,files in os.walk(directory):
        dirs[:]=[name for name in dirs if name not in {'.git','.repo'} and not (Path(current)/name).is_symlink()]
        for name in files:
            path=Path(current)/name
            if path.is_symlink() or str(path.relative_to(directory)) in tracked: continue
            if path.suffix not in {'.o','.cmd','.a','.d','.su'}: continue
            assert path.resolve().is_relative_to(directory.resolve())
            items.append({'path':str(path),'bytes':path.stat().st_size,'reason':'Git未跟踪的编译对象、编译依赖或命令缓存'})
update=sdk/'output/update'
update_files=[]
for path in update.rglob('*'):
    if not path.is_file() or path.is_symlink():continue
    with path.open('rb') as stream: magic=stream.read(4)
    update_files.append({'path':str(path),'bytes':path.stat().st_size,'magic':magic.hex()})
    if magic==b'RKAF':
        items.append({'path':str(path),'bytes':path.stat().st_size,'reason':'整机打包内部中间文件；最终RKFW镜像在正式目录保留'})
result={'root':str(sdk),'files':items,'bytes':sum(item['bytes'] for item in items),'count':len(items),'update_files':update_files}
Path(__file__).with_name('linux_plan.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'count':result['count'],'bytes':result['bytes'],'update_files':update_files},ensure_ascii=False))
