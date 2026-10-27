import json
import os
from pathlib import Path
import signal
from common import WORK, save

stage = WORK / 'qc_page_gate_04'
receipt = json.loads((stage / 'qt_test.process.json').read_text())
pid = receipt['pid']
argv = (Path('/proc') / str(pid) / 'cmdline').read_bytes().split(b'\0')
assert argv[0].decode() == str(WORK / 'page_test_build/qc_local_page_test')
assert any(str(stage / 'qt.txt').encode() in value for value in argv)
save(stage / 'diagnostic_stop.json', {'pid': pid, 'signal': 'SIGTERM',
     'reason': 'Unexpected dirty confirmation while Ctrl+N clears batch selection: selection signal re-enters editor before fields reset. Original failure preserved; no completion claim.'})
os.kill(pid, signal.SIGTERM)
