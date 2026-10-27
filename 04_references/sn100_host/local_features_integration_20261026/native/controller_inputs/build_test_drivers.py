#!/usr/bin/env python3
"""Fresh test-only compilation against this cohort's actual new native objects; no GUI."""
import argparse
import os
from pathlib import Path
import re
from common import PROGRAM, NATIVE, FREEZE, DRIVERS, sha, save, load, now, require, verify_native, verify_controller_inputs, run_logged


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go',action='store_true',required=True)
    parser.add_argument('--components-approved',action='store_true',help='Root explicitly authorizes all six proposed components')
    parser.add_argument('--extra-driver',action='append',default=[],help='Later real shell driver stems; source/pro must already exist')
    parser.add_argument('--timeout-seconds',type=int,default=2400)
    args=parser.parse_args()
    require(args.root_go and os.name=='posix' and os.geteuid()!=0 and 1<=args.timeout_seconds<=7200,
            'Root go, ordinary WSL user and bounded phase required')
    require(not DRIVERS.exists(),'Fresh driver directory required; preserve failures in their original directory')
    control_before=verify_controller_inputs()
    registry=load(Path(__file__).with_name('driver_registry.json'))
    rows=list(registry['legacy'])
    if args.components_approved:rows+=registry['components_proposed']
    for name in args.extra_driver:
        require(re.fullmatch(r'[a-z][a-z0-9_]*',name),'Unsafe extra driver stem')
        rows.append({'name':name,'source_relative':'tests/'+name+'.cpp','project_relative':'tests/'+name+'.pro'})
    names=[row['name'] for row in rows]
    require(len(names)==len(set(names)),'Driver list has duplicates')
    DRIVERS.mkdir(parents=True)
    try:
        frozen=load(FREEZE);native=verify_native(frozen)
        save(DRIVERS/'controller_inputs_before.json',control_before)
        inputs={}
        for row in rows:
            for key in ('source_relative','project_relative'):
                relative=Path(row[key]);require(not relative.is_absolute() and '..' not in relative.parts,'Unsafe test input')
                path=(PROGRAM/relative).resolve(strict=True)
                require(path.is_relative_to(PROGRAM.resolve()),'Test source escaped PROGRAM')
                inputs[str(path)]=sha(path)
        save(DRIVERS/'test_inputs_before.json',inputs)
        save(DRIVERS/'requested_drivers.json',rows)
        command=['python3','-B',str(PROGRAM/'tests/build_shell_drivers.py'),
                 '--build-dir',str(NATIVE/'wsl_release'),'--snapshot',str(FREEZE),
                 '--output-dir',str(DRIVERS),'--jobs','2',*names]
        # Deliberately no --reuse-dir: a new cohort never takes old test binaries.
        actual=run_logged('binding_build',command,DRIVERS,timeout=args.timeout_seconds)
        proof=load(DRIVERS/'binding_proof.json')
        require(set(proof['drivers'])==set(names) and proof['snapshot_sha256']==sha(FREEZE)
                and proof['native_makefile_sha256']==native['makefile_sha256'],'Fresh binding identity differs')
        for row in rows:
            record=proof['drivers'][row['name']]
            require(record['source_relative']==row['source_relative'] and record['source_sha256']==inputs[str((PROGRAM/row['source_relative']).resolve())],
                    'Bound source differs from the real requested test')
            require('reused_from' not in record and len(record['receipts'])==3
                    and all(x['exit_code']==0 for x in record['receipts']),'Each driver must have actual fresh moc/compile/link0')
        after={name:sha(name) for name in inputs};save(DRIVERS/'test_inputs_after.json',after)
        require(after==inputs,'Test source/project changed during compilation')
        verify_native(frozen);control_after=verify_controller_inputs()
        save(DRIVERS/'controller_inputs_after.json',control_after);require(control_after==control_before,'Controller changed')
        save(DRIVERS/'verification.json',{'schema':'sn100-local-features-driver-build-v1','passed':True,
             'finished_utc':now(),'actual_exit':actual['exit_code'],'source_inputs':len(frozen),
             'snapshot_sha256':sha(FREEZE),'drivers':names,'driver_count':len(names),
             'native_objects_excluding_main':proof['native_objects_excluding_main'],
             'components_explicitly_approved':args.components_approved,'parallel_jobs_maximum':2,
             'reused_test_drivers':0,'GUI_or_component_behavior_run':False,
             'scope':'Fresh test-only moc/compile/link against actual new native objects; no GUI/SQL runtime acceptance inferred'})
    except BaseException as error:
        save(DRIVERS/'failure.json',{'error_type':type(error).__name__,'error':str(error),'failed_utc':now(),'old_outputs_preserved':True})
        raise


if __name__=='__main__':main()
