"""补足原项目页必读的匿名试剂目录；不读取任何参考或现场数据库。"""
from pathlib import Path
import sqlite3
root = Path(__file__).resolve().parent / 'original_fixture/db/userSet'
root.mkdir(parents=True, exist_ok=True)
target = root / 'reagent_Information.db'
if target.exists():
    raise SystemExit('拒绝覆盖')
with sqlite3.connect(target) as db:
    db.execute('CREATE TABLE reagent_list(id INTEGER PRIMARY KEY,reagent_name TEXT,reagent_type TEXT,valid INTEGER)')
    db.executemany('INSERT INTO reagent_list VALUES(?,?,?,?)', [(1,'RK_R1','r_reagent',1),(2,'RK_IBS','s_ibs',1),(3,'RK_CLEAN','cleaner',1)])
    db.execute('CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_name TEXT,batch_group TEXT)')
print('ANONYMOUS_REAGENT_CHOICES_CREATED=1')
