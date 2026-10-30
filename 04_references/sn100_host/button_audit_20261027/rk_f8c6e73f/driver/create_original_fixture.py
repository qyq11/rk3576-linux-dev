"""只创建匿名原表结构，供RK按钮条件检查；这些参数不用于仪器检测。"""
from pathlib import Path
import json
import sqlite3

root = Path(__file__).resolve().parent / 'original_fixture'
if root.exists():
    raise SystemExit('拒绝覆盖已有夹具')
(root / 'project').mkdir(parents=True)
(root / 'db/systemInfo').mkdir(parents=True)
(root / '.sn100-original-work-v1').write_bytes(b'sn100-original-work-v1\n')
with sqlite3.connect(root / 'db/systemInfo/option_Unit.db') as db:
    db.execute('CREATE TABLE option_unit(id INTEGER PRIMARY KEY,unit TEXT,type TEXT,ratio TEXT)')
    db.executemany('INSERT INTO option_unit VALUES(?,?,?,?)', [(1,'s','Original','1'),(2,'%','Rate','1'),(3,'INR','INR','1')])
base = dict(xuhao='7',project_id='9',project_en_name='RK_AUDIT_PT',project_cn_name='匿名按钮核查项目',
    project_lis_code='RK_AUDIT',is_used='1',fixed='0',is_related='0',change_time='2026-10-27',
    xishu_a='1',xishu_b='0',sample_warmup_length='0',auto_apply='0',min_test_time='1',
    max_test_time='60',stopTime='0',sample_change_clean='0',first_light='405',second_light='0',IBS_name='')
sample_columns = ['xuhao','sample_rate','sample_default_option','sample_add_mode','IBS_amount_ul','sample_amount_ul',
    'mix_amount_ul','second_IBS_amount_ul','action_amount_ul','blender_type','blender_time_sec','calibration_option',
    'sample_option','before_clean_frequency','before_clean_amount_ul','before_clean_name','after_clean_frequency']
reagent_columns = ['xuhao','reagent_name','reagent_warmup_length','reagent_amount_ul','needle_select',
    'blender_type','blender_time_sec','before_clean_frequency','before_clean_amount_ul','before_clean_name','after_clean_frequency']
with sqlite3.connect(root / 'project/RK_AUDIT_PT.db') as db:
    db.execute('CREATE TABLE project_base(name TEXT,value TEXT)')
    db.executemany('INSERT INTO project_base VALUES(?,?)', base.items())
    db.execute('CREATE TABLE AlgParam(key TEXT,value TEXT)')
    db.execute("INSERT INTO AlgParam VALUES('RK_AUDIT_ONLY','not_for_detection')")
    db.execute('CREATE TABLE project_sample_rate('+','.join('"'+c+'" TEXT' for c in sample_columns)+')')
    sample = dict.fromkeys(sample_columns, '0')
    sample.update(xuhao='1',sample_rate='1/1',sample_default_option='1',sample_option='1',sample_amount_ul='20',before_clean_name='')
    db.execute('INSERT INTO project_sample_rate VALUES('+','.join('?' for _ in sample_columns)+')', [sample[c] for c in sample_columns])
    db.execute('CREATE TABLE project_reagent('+','.join('"'+c+'" TEXT' for c in reagent_columns)+')')
    db.execute('CREATE TABLE project_results(result_id TEXT,result_type TEXT,result_name TEXT,decimal_point TEXT,unit_name TEXT,test_low_range TEXT,test_up_range TEXT,normal_low_range TEXT,normal_up_range TEXT,is_used TEXT,display TEXT,main_result TEXT,lis_code TEXT)')
    for index, kind in enumerate(['Original','Calibration','Rate','INR','MDA'],1):
        db.execute('INSERT INTO project_results VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)',
            (str(index),kind,'匿名'+kind,'2','s','0','999','0','999','1','1','1' if index==1 else '0','RK_AUDIT'))
    db.execute('CREATE TABLE project_mda(name TEXT,rate_select TEXT)')
    db.execute('CREATE TABLE project_quality(control_name TEXT,sample_rate TEXT)')
(root / 'fixture_scope.json').write_text(json.dumps({'anonymous':True,'purpose':'只核原库模式按钮条件，参数不用于检测',
    'source':'正式original_project_repository读取要求及original_detection_rk_test匿名七表结构',
    'contains_patient_or_account_data':False},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('ANONYMOUS_ORIGINAL_FIXTURE_CREATED=1')
