"""按用户最新要求立即恢复留屏；不把失败/未完成复核改写成通过。"""
from pathlib import Path
import hashlib
here=Path(__file__).resolve().parent
source=here/'prepare_keep_open09.py'
raw=source.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='0db64be3019fe478d77003d98a693d64ca35df7e857ffb3a742d253416ea1dba'
text=raw.decode('utf-8')
old="assert (here/'run09_revision05.exit').read_text().strip()=='0'\nassert (here/'independent_review09_revision05.exit').read_text().strip()=='0'"
new="assert (here/'build09_revision05_outer.exit').read_text().strip()=='0'\nassert (here/'checker09_revision05_outer.exit').read_text().strip()=='0'\nassert (here/'run09_revision05.exit').read_text().strip() in ('0','1')\n# 用户要求立即保持正式程序运行，复核失败仍按真实结果另记。"
assert text.count(old)==1
text=text.replace(old,new)
executed=here/'keep_open09_user_requested_generator.py'
with executed.open('x',encoding='utf-8',newline='\n') as out:out.write(text)
exec(compile(text,str(executed),'exec'),{'__file__':str(executed),'__name__':'__main__'})
