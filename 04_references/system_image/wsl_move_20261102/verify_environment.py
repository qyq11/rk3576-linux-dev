from pathlib import Path
import hashlib,json,os,struct,subprocess

here=Path(__file__).resolve().parent
disk=json.loads((here/'disk_move_result.json').read_text())
assert disk['whole_disk_matches'] and disk['move_exit']==0
assert os.environ.get('WSL_DISTRO_NAME')=='RK3576-Build-22.04'
subprocess.run(['python3',str(here/'inspect_environment.py'),str(here/'environment_after.json')],check=True)
before=json.loads((here/'environment_before.json').read_text())
after=json.loads((here/'environment_after.json').read_text())
differences=[key for key in before if before[key]!=after.get(key)]
assert not differences,differences
sdk=Path(after['sdk'])
tools=sdk/'prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin'
source_c=here/'migration_probe.c'
source_c.write_text('#include <stdint.h>\n#include <stdio.h>\nint main(void) { const uint64_t value=3576; printf("%llu\\n", (unsigned long long)value); return 0; }\n')
source_cpp=here/'migration_probe.cpp'
source_cpp.write_text('#include <vector>\nint main() { std::vector<int> values{3,5,7,6}; return values.size()==4 ? 0 : 1; }\n')
checks=[]
for label,compiler,source,standard in [('C','aarch64-none-linux-gnu-gcc',source_c,'-std=c11'),('C++','aarch64-none-linux-gnu-g++',source_cpp,'-std=c++17')]:
    output=here/('migration_'+label.replace('+','p')+'.elf')
    result=subprocess.run([str(tools/compiler),standard,'-O2','-Wall','-Wextra',str(source),'-o',str(output)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    data=output.read_bytes()
    assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183
    header=subprocess.check_output(['readelf','-h',str(output)]).decode()
    checks.append({'target':label,'passed':True,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'readelf':header,'executed':False})
overlay=here/'migration_probe.dts'
overlay.write_text('/dts-v1/;\n/plugin/;\n/ { fragment@0 { target-path = "/"; __overlay__ { migration-check = "wsl-d-drive"; }; }; };\n')
dtbo=here/'migration_probe.dtbo'
subprocess.run([str(sdk/'kernel-6.1/scripts/dtc/dtc'),'-@','-I','dts','-O','dtb','-o',str(dtbo),str(overlay)],check=True,capture_output=True)
assert dtbo.read_bytes()[:4]==b'\xd0\x0d\xfe\xed'
result={'scope':'只验证电脑端WSL启动、内容保留、ARM64编译链接及设备树编译；未重编完整系统，未运行ARM64程序，未连接或改写RK板。','inventory_identical':True,'different_inventory_fields':differences,'c_and_cpp_compile_link':checks,'device_tree_compilation':{'passed':True,'bytes':dtbo.stat().st_size},'project_access_from_wsl':after['project_access'],'sdk_build_entry_exists':(sdk/'build.sh').is_file(),'whole_disk_verified_before_boot':True}
(here/'verification_result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print('MIGRATION_VERIFICATION=passed C=ARM64 CXX=ARM64 DTS=passed INVENTORY=identical')
