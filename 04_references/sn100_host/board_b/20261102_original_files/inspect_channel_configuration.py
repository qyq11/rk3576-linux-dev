"""只读检查待部署原通道配置的数量，不读取患者或账户资料。"""
from pathlib import Path
import json
import sqlite3

out = Path(__file__).resolve().parent
path = out / 'working/db/systemInfo/channel.db'
report = {'scope': '配置源文件只读盘点，不是电脑功能测试', 'file': 'db/systemInfo/channel.db', 'tables': {}}
with sqlite3.connect(path.as_uri() + '?mode=ro&immutable=1', uri=True) as database:
    for table in ('test_channel', 'warm_channel', 'cache_channel'):
        total, enabled = database.execute(f'SELECT COUNT(*), SUM(CASE WHEN is_open=1 THEN 1 ELSE 0 END) FROM "{table}"').fetchone()
        report['tables'][table] = {'total': total, 'enabled': enabled}
(out / 'channel_configuration_inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
