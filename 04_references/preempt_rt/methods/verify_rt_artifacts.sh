set -euo pipefail
export LC_ALL=C
sdk=/home/qyq/rk3576/TaishanPi-3-Linux
work=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022
cd "$work/artifacts"
test "$(cat "$work/build.exit")" = 0
modinfo -F vermagic "$work/staging/lib/modules/6.1.99-rt36-tspi/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko"
"$sdk/rkbin/tools/mkimage" -l boot.img
"$work/build/scripts/resource_tool" --help > "$work/resource_help.txt" 2>&1 || true
python3 - <<'PY'
import hashlib, json, pathlib, subprocess
work=pathlib.Path('/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022')
sdk=pathlib.Path('/home/qyq/rk3576/TaishanPi-3-Linux')
art=work/'artifacts'
image=(art/'boot.img').read_bytes()
def prop(node, key, kind='u'):
    return subprocess.check_output(['fdtget','-t',kind,str(art/'boot.img'),node,key],text=True).strip()
parts={}
for name, filename in [('kernel','Image'),('fdt','kernel.dtb'),('resource','resource.img')]:
    node='/images/'+name
    offset=int(prop(node,'data-position'))
    length=int(prop(node,'data-size'))
    payload=image[offset:offset+length]
    assert len(payload)==length
    assert payload==(art/filename).read_bytes()
    expected=b''.join(int(h,16).to_bytes(4,'big') for h in prop(node+'/hash','value','x').split()).hex()
    actual=hashlib.sha256(payload).hexdigest()
    assert actual==expected,(name,actual,expected)
    assert prop(node,'compression','s')=='none'
    parts[name]={'offset':offset,'bytes':length,'sha256':actual}
assert len(image)<=67108864
assert (art/'kernel.dtb').read_bytes()==(sdk/'kernel-6.1/arch/arm64/boot/dts/rockchip/tspi-3m-rk3576-panfrost.dtb').read_bytes()
assert (art/'kernel.config').read_bytes()==(art/'image.config').read_bytes()==(work/'build/.config').read_bytes()
assert 'CONFIG_PREEMPT_RT=y\n' in (art/'kernel.config').read_text()
assert 'CONFIG_HZ=250\n' in (art/'kernel.config').read_text()
modules=sorted((work/'staging/lib/modules/6.1.99-rt36-tspi').rglob('*.ko'))
for module in modules:
    vermagic=subprocess.check_output(['modinfo','-F','vermagic',str(module)],text=True).strip()
    assert vermagic=='6.1.99-rt36-tspi SMP preempt_rt mod_unload aarch64',vermagic
source_modules=sorted(p.relative_to(sdk/'kernel-6.1').as_posix() for p in (sdk/'kernel-6.1').rglob('*.ko'))
new_modules=sorted(p.relative_to(work/'staging/lib/modules/6.1.99-rt36-tspi').as_posix().removeprefix('kernel/') for p in modules)
assert source_modules==new_modules,(set(source_modules)-set(new_modules),set(new_modules)-set(source_modules))
summary={'kernelrelease':'6.1.99-rt36-tspi','base_commit':'bf3e03f69e6a3b766dc6356a7475e8ad64a48a98',
 'boot_bytes':len(image),'boot_partition_bytes':67108864,'fit_payloads':parts,
 'config_matches_image':True,'device_tree_matches_original':True,'module_count':len(modules),
 'all_module_vermagic_matches':True,'module_paths_match_original_build':True,'fit_payload_hashes_verified':True,
 'signature_verification':'not performed; same original SDK unsigned packaging path',
 'build_exit':0,'deployed_boot':False}
(art/'verification.json').write_text(json.dumps(summary,indent=2)+'\n')
names=['Image','kernel.dtb','kernel.config','resource.img','boot.img','modules-6.1.99-rt36-tspi.tar.gz','verification.json']
(art/'SHA256SUMS').write_text(''.join(hashlib.sha256((art/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in names))
print(json.dumps(summary,indent=2))
PY
cat SHA256SUMS
printf 'ARTIFACT_VERIFY=PASS\n'
