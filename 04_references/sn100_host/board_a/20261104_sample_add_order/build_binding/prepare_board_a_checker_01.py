"""固定仅改板卡身份的检查器，并沿同批15对象准备独立A交叉编译。"""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
parent=HERE/'root_sample_checker14_review_02'
candidate=ROOT/'temp/sn100_host/sample_add_order_board_a_20261104_01/sample_add_review.cpp'
assert sha(candidate)=='e6116eb485fcf272e5a5a86005d92eb6700006b492679619bd455cf5c40ad677'
assert sha(parent/'sample_add_review.cpp')=='100dea76ed8f8017764630257b8a218abaa36e0ac67da5bcbd851043ae4f25d1'
raw=(parent/'sample_add_review.cpp').read_bytes()
for before,after in [(b'board_b',b'board_a'),(b'8314b1e8ffd0321f',b'3bf8222fe1417f6a'),('板B'.encode(),'板A'.encode())]:
    assert raw.count(before)==(2 if before=='板B'.encode() else 1)
    raw=raw.replace(before,after)
assert raw==candidate.read_bytes()
review=HERE/'root_sample_checker_board_a_review_01'
review.mkdir()
data=json.loads((parent/'root_snapshot.json').read_bytes())
records=[]
for entry in data['records']:
    old=ROOT/entry['copy']['path']
    assert sha(old)==entry['copy']['sha256']
    source=candidate if old.name=='sample_add_review.cpp' else old
    destination=review/old.name
    with destination.open('xb') as stream: stream.write(source.read_bytes())
    records.append({'copy':{'path':destination.relative_to(ROOT).as_posix(),'sha256':sha(destination),'bytes':destination.stat().st_size},
        'parent_source':str(source),'scope':'board_a_identity_only' if old.name=='sample_add_review.cpp' else 'unchanged_parent_original_metadata'})
snapshot=review/'root_snapshot.json'
snapshot.write_text(json.dumps({'records':records,'board':'board_a','identity_only_delta':True},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
oldwrapper=HERE/'sample_checker15_build_wrapper_root_01/prepare_and_build.py'
assert sha(oldwrapper)=='a31e8e7e91f61ad7fb1c895cef4ebd39dd9585c57e380260a874997f50f996ae'
text=oldwrapper.read_text(encoding='utf-8')
changes={'root_sample_checker14_review_02':'root_sample_checker_board_a_review_01',
    '87ded8cdd142b25ad1d3a6dd9daac4cb7c24c3165ecbbc946d5d0058e81d3b20':sha(snapshot),
    '100dea76ed8f8017764630257b8a218abaa36e0ac67da5bcbd851043ae4f25d1':sha(candidate),
    'sample_checker15_exec_02_':'sample_checker15_board_a_exec_01_'}
for before,after in changes.items():
    assert before in text
    text=text.replace(before,after)
wrapperdir=HERE/'sample_checker15_board_a_wrapper_01'
wrapperdir.mkdir()
wrapper=wrapperdir/'prepare_and_build.py'
wrapper.write_text(text,encoding='utf-8',newline='\n')
import ast
ast.parse(text)
report={'board':'board_a','parent_wrapper_sha256':sha(oldwrapper),'wrapper_sha256':sha(wrapper),'snapshot_sha256':sha(snapshot),
    'checker_sha256':sha(candidate),'only_wrapper_changes':changes,'product_executed':False,'compiled':False}
(wrapperdir/'preparation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
