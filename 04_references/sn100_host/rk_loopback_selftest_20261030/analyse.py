"""仅分析RK产生的原始日志与采样，不在电脑运行产品或模拟功能测试。"""
from pathlib import Path
import collections,functools,hashlib,json,math,operator,re,statistics
w=Path(__file__).resolve().parent;folder=w/'completed_01';log=(folder/'app.log').read_text();runtime=(folder/'runtime.txt').read_text();errors=[]
expected={'TX':b'Qj','RX':b'Qj[4][SIMULATED][2][1][0][0]'}
pattern=re.compile(r'^STATUS_LOOPBACK_WIRE\s+run=\s*(\d+)\s+generation=\s*(\d+)\s+sequence=\s*(\d+)\s+elapsed_ms=\s*(\d+)\s+(TX|RX)\s+HEX=\s*(.*)$',re.M)
events=[]
for match in pattern.finditer(log):
    run,generation,sequence,when=map(int,match.group(1,2,3,4));direction=match.group(5);raw=bytes.fromhex(match.group(6))
    events.append({'run':run,'generation':generation,'sequence':sequence,'elapsed_ms':when,'direction':direction,'hex':raw.hex(' '),'raw':raw})
if not events:errors.append('no_wire_events')
if [e['sequence'] for e in events]!=list(range(1,len(events)+1)):errors.append('wire_sequence_not_contiguous')
buffers=collections.defaultdict(bytes);frames=[]
for e in events:
    key=(e['run'],e['generation'],e['direction']);buffer=buffers[key]+e['raw'];direction=e['direction']
    header,tail=(b'\x02\x03',b'\x04\x05') if direction=='TX' else (b'\x06\x07',b'\x08\x09')
    while len(buffer)>=4:
        if buffer[:2]!=header:errors.append('bad_header');buffer=b'';break
        length=int.from_bytes(buffer[2:4],'big')
        if not 5<=length<=2004:errors.append('bad_length');buffer=b'';break
        if len(buffer)<length+7:break
        frame,buffer=buffer[:length+7],buffer[length+7:];packet=frame[4:4+length]
        if frame[-2:]!=tail:errors.append('bad_tail')
        if frame[4+length]!=functools.reduce(operator.xor,packet,0):errors.append('bad_xor')
        if packet[:4]!=b'\0\0\0\1':errors.append('unexpected_fragment_metadata')
        payload=packet[4:]
        if payload!=expected[direction]:errors.append('unexpected_payload')
        frames.append({'sequence':e['sequence'],'generation':e['generation'],'elapsed_ms':e['elapsed_ms'],'direction':direction,'payload':payload.decode('ascii',errors='replace'),'frame_hex':frame.hex(' ')})
    buffers[key]=buffer
if any(buffers.values()):errors.append('incomplete_frame_at_end')
pending=collections.deque();pairs=[];tx=[];rx=[];overlap=0
for f in frames:
    if f['direction']=='TX':
        if pending:overlap+=1
        pending.append(f);tx.append(f)
    else:
        rx.append(f)
        if not pending:errors.append('response_without_query');continue
        request=pending.popleft()
        if request['generation']!=f['generation']:errors.append('cross_generation_pair')
        delta=f['elapsed_ms']-request['elapsed_ms']
        if delta<0:errors.append('negative_observation_interval')
        pairs.append({'tx_sequence':request['sequence'],'rx_sequence':f['sequence'],'peer_read_to_client_read_ms':delta})
if pending:errors.append('unanswered_query_at_end')
if overlap:errors.append('query_overlap_or_retry_observed')
stop=re.search(r'STATUS_LOOPBACK_WORKER_STOPPED\s+(\d+)\s+wire_events=\s*(\d+)\s+wire_bytes=\s*(\d+)',log)
if not stop:errors.append('worker_stop_missing')
else:
    if int(stop.group(2))!=len(events):errors.append('worker_wire_count_mismatch')
    if int(stop.group(3))!=sum(len(e['raw']) for e in events):errors.append('worker_wire_bytes_mismatch')
if 'STATUS_LOOPBACK_ERROR' in log:errors.append('loopback_error_logged')
for marker in ['STATUS_LOOPBACK_STARTED self_owned_peer=127.0.0.1 instrument=false','STATUS_LOOPBACK_STOPPED true','THREADS_STOPPED true','EXIT code= 0','HOSPITAL_STORE_CLOSED','ACCOUNT_STORE_CLOSED','HISTORY_STORE_CLOSED','PROJECT_STORE_CLOSED','REAGENT_STORE_CLOSED','SAMPLE_STORE_CLOSED','MAINTENANCE_STORE_CLOSED']:
    if marker not in log:errors.append('missing_'+marker)
actual_exit=int((folder/'exit_code').read_text().strip())
if actual_exit!=0:errors.append('nonzero_exit')
if 'FORCED_KILL=0' not in runtime:errors.append('forced_exit')
observed=re.search(r'OBSERVED_QUERY_SECONDS=(\d+)',runtime);seconds=int(observed.group(1)) if observed else 0
if seconds<300:errors.append('duration_less_than_300_seconds')
if len(tx)<299:errors.append('unexpectedly_low_query_count')
args=(folder/'arguments.txt').read_text().splitlines()
if '--instrument-control' in args or '--original-db-root' in args:errors.append('unexpected_instrument_or_original_data_mode')
sockets=(folder/'product_sockets.txt').read_text()
if len(sockets.splitlines())<2:errors.append('socket_observation_missing')
for line in sockets.splitlines():
    columns=line.split()
    if len(columns)<5 or not all(addr.startswith('127.0.0.1:') for addr in columns[3:5]):errors.append('non_loopback_socket')
def distribution(values):
    a=sorted(values)
    if not a:return None
    return {'n':len(a),'min':a[0],'p50':a[math.ceil(.5*len(a))-1],'p95':a[math.ceil(.95*len(a))-1],'p99':a[math.ceil(.99*len(a))-1],'max':a[-1]}
raw_rows=[line.split('\t') for line in (folder/'resources.tsv').read_text().splitlines()[1:]]
rows=[a for a in raw_rows if len(a)>=9 and all(a[i].replace('.','',1).isdigit() for i in range(7))]
if not rows:errors.append('no_resource_samples')
resource={}
if rows:
    first,last=rows[0],rows[-1];span=float(last[0])-float(first[0]);hz=next(int(a) for a in runtime.splitlines() if a.isdigit())
    temps=[int(v) for a in rows for v in a[7].split(';') if v.isdigit()];freqs=[int(v) for a in rows for v in a[8].split(';') if v.isdigit()]
    resource={'samples':len(rows),'sampling_seconds':span,'clock_ticks_per_second':hz,'cpu_percent_single_core':(int(last[1])+int(last[2])-int(first[1])-int(first[2]))/hz/span*100,
        'peak_rss_kib':max(int(a[3]) for a in rows),'peak_pss_kib':max(int(a[4]) for a in rows),'peak_threads':max(int(a[5]) for a in rows),'peak_fds':max(int(a[6]) for a in rows),
        'first_60_samples_pss_median_kib':statistics.median(int(a[4]) for a in rows[:60]),'last_60_samples_pss_median_kib':statistics.median(int(a[4]) for a in rows[-60:]),
        'max_temperature_mC':max(temps) if temps else None,'observed_frequency_min_kHz':min(freqs) if freqs else None,'observed_frequency_max_kHz':max(freqs) if freqs else None,
        'scope':'循环连接就绪后的约1秒采样，不包含启动阶段；未确认资源预算，频率变化不能直接认定温度降频。'}
result={'passed':not errors,'errors':errors,'actual_exit_code':actual_exit,'requested_seconds':300,'observed_seconds':seconds,'tx_messages':len(tx),'rx_messages':len(rx),'paired_exchanges':len(pairs),'overlap_or_retry_sequences':overlap,
    'wire_events':len(events),'wire_bytes':sum(len(e['raw']) for e in events),'runs':sorted({e['run'] for e in events}),'connection_generations':sorted({e['generation'] for e in events}),
    'gui_forwarded_tx_events':len(re.findall(r'^STATUS_LOOPBACK_TX ',log,re.M)),'gui_forwarded_rx_events':len(re.findall(r'^STATUS_LOOPBACK_RX ',log,re.M)),
    'tx_interval_ms':distribution([b['elapsed_ms']-a['elapsed_ms'] for a,b in zip(tx,tx[1:])]),'peer_read_to_client_read_ms':distribution([p['peer_read_to_client_read_ms'] for p in pairs]),
    'timing_scope':'后台毫秒级单调时钟：从模拟端实际读到请求至客户端实际读到回复，不含发送排队，不是完整往返、界面/屏幕延迟或真实仪器ACK。',
    'resources':resource,'real_instrument':False,'physical_input':False,'main_sha256':'c16b44357595b524a3cea1ed71574a6e2809b9968cea0076a86ff2f336674ef4','release':'ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14'}
(folder/'wire_events.json').write_text(json.dumps([{k:v for k,v in e.items() if k!='raw'} for e in events],indent=2)+'\n')
(folder/'decoded_frames.json').write_text(json.dumps(frames,indent=2)+'\n');(folder/'exchange_pairs.json').write_text(json.dumps(pairs,indent=2)+'\n')
(w/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False,indent=2));assert not errors,errors
