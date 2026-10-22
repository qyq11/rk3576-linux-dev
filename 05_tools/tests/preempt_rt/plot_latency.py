"""Recompute the recorded four runs and plot them locally; does not access the RK board."""
from pathlib import Path
import csv
import hashlib
import json
import math
import zipfile

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Patch
from matplotlib.ticker import FuncFormatter
import numpy as np

PROJECT = Path(__file__).resolve().parents[3]
DATA = PROJECT / '04_references/preempt_rt'
PHOTOS = PROJECT / '03_photos'
COLORS = {'normal': '#356C9B', 'rt': '#CE763A'}
LABELS = {'normal': '普通内核 6.1.99', 'rt': 'PREEMPT_RT 6.1.99-rt36-tspi'}
SCENARIOS = ('idle', 'cpu_stress')
SCENARIO_NAMES = ('空载', '8 核 CPU 压力')

available = {font.name for font in font_manager.fontManager.ttflist}
for preferred in ('Microsoft YaHei', 'Noto Sans CJK SC', 'SimHei'):
    if preferred in available:
        plt.rcParams['font.family'] = preferred
        break
else:
    raise RuntimeError('Install a Chinese font: Microsoft YaHei, Noto Sans CJK SC, or SimHei')
plt.rcParams.update({'font.size': 11, 'axes.unicode_minus': False, 'svg.fonttype': 'none',
                     'axes.spines.top': False, 'axes.spines.right': False,
                     'axes.edgecolor': '#CBD2DA', 'text.color': '#233342',
                     'axes.labelcolor': '#415263', 'xtick.color': '#415263',
                     'ytick.color': '#415263', 'figure.facecolor': 'white'})

def digest(data):
    return hashlib.sha256(data).hexdigest()

def load_runs():
    output = {}
    archives = {'normal': 'normal_baseline.zip', 'rt': 'rt_validation.zip'}
    metadata = json.loads((DATA / 'rt_summary.json').read_text(encoding='utf-8'))
    for kernel, filename in archives.items():
        path = DATA / filename
        if kernel == 'rt':
            assert digest(path.read_bytes()) == metadata['archive_sha256']
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            manifest_name = 'normal.sha256' if kernel == 'normal' else metadata['run'] + '.sha256'
            for line in archive.read(manifest_name).decode().splitlines():
                sha, name = line.split('  ', 1)
                assert digest(archive.read(name)) == sha, name
            output[kernel] = {}
            for phase in SCENARIOS:
                raw = json.loads(archive.read(phase + '.json'))
                assert raw['return_code'] == 0
                assert raw['num_threads'] == 8
                assert raw['sysinfo']['realtime'] == (kernel == 'rt')
                histogram = {}
                threads = list(raw['thread'].values())
                for thread in threads:
                    for latency, count in thread['histogram'].items():
                        latency = int(latency)
                        histogram[latency] = histogram.get(latency, 0) + count
                cycles = sum(thread['cycles'] for thread in threads)
                assert sum(histogram.values()) == cycles
                accumulated = 0
                p999 = None
                for latency, count in sorted(histogram.items()):
                    accumulated += count
                    if accumulated >= math.ceil(cycles * 0.999):
                        p999 = latency
                        break
                stats = {
                    'kernel': kernel, 'scenario': phase, 'cycles': cycles,
                    'mean_us': round(sum(t['avg'] * t['cycles'] for t in threads) / cycles, 2),
                    'p999_us': p999, 'max_us': max(t['max'] for t in threads),
                }
                reference = metadata[kernel][phase]
                assert stats['mean_us'] == reference['weighted_avg_us']
                assert stats['p999_us'] == reference['p999_us']
                assert stats['max_us'] == reference['max_us']
                output[kernel][phase] = {'stats': stats, 'histogram': histogram}
    return output, archives

def save_figure(fig, basename):
    for suffix in ('.png', '.svg'):
        fig.savefig(PHOTOS / (basename + suffix), dpi=190, facecolor='white')
    plt.close(fig)

runs, archives = load_runs()
rows = [runs[kernel][phase]['stats'] for kernel in ('normal', 'rt') for phase in SCENARIOS]
csv_path = DATA / 'latency_comparison.csv'
with csv_path.open('w', newline='', encoding='utf-8') as output:
    writer = csv.DictWriter(output, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)

fig, axes = plt.subplots(1, 3, figsize=(12.8, 5.35))
fig.subplots_adjust(left=0.065, right=0.985, top=0.72, bottom=0.22, wspace=0.34)
fig.suptitle('普通内核与 PREEMPT_RT：唤醒延迟对比', x=0.5, y=0.975, fontsize=20, weight='bold')
fig.text(0.5, 0.885, 'RK3576 · 每个内核、每个场景各 60 s · 8 线程 / 1 ms 周期 / FIFO 80', ha='center', fontsize=11)
fig.legend(handles=[Patch(facecolor=COLORS[k], label=LABELS[k]) for k in ('normal', 'rt')],
           loc='upper center', bbox_to_anchor=(0.5, 0.865), ncol=2, frameon=False, fontsize=11)
metrics = (('mean_us', '(a) 平均值', 24), ('p999_us', '(b) P99.9', 235), ('max_us', '(c) 最大值', 455))
x = np.arange(2)
for ax, (metric, title, upper) in zip(axes, metrics):
    ax.set_axisbelow(True)
    ax.grid(axis='y', color='#E5EAF0', linewidth=0.8)
    for kernel, offset in (('normal', -0.19), ('rt', 0.19)):
        values = [runs[kernel][phase]['stats'][metric] for phase in SCENARIOS]
        bars = ax.bar(x + offset, values, width=0.32, color=COLORS[kernel], zorder=3)
        ax.bar_label(bars, labels=[f'{v:.2f}' if metric == 'mean_us' else str(v) for v in values],
                     padding=5, fontsize=11, color='#233342')
    ax.set_xticks(x, SCENARIO_NAMES)
    ax.set_ylim(0, upper)
    ax.set_title(title, fontsize=13, loc='left', pad=15, weight='bold')
    ax.set_ylabel('唤醒延迟 / μs')
    ax.tick_params(axis='x', length=0, pad=10)
fig.text(0.065, 0.085, 'P99.9：本轮至少 99.9% 的样本不超过该值。数值越低，延迟越小。', fontsize=10, color='#526373')
fig.text(0.065, 0.035, '单轮短测，无误差棒；压力下 RT 最大值略高。结果不代表长期上界或业务实时性验收。', fontsize=10, color='#526373')
save_figure(fig, '20261022_rt_latency_comparison_01')

fig, axes = plt.subplots(1, 2, figsize=(12.2, 5.1))
fig.subplots_adjust(left=0.08, right=0.98, top=0.71, bottom=0.23, wspace=0.24)
fig.suptitle('延迟尾部分布：超过给定延迟的样本比例', y=0.98, fontsize=19, weight='bold')
fig.text(0.5, 0.89, '由原始整数微秒直方图计算 · 合并 CPU0–7 的样本 · 纵轴为对数刻度', ha='center', fontsize=11)
fig.legend(handles=[Patch(facecolor=COLORS[k], label=LABELS[k]) for k in ('normal', 'rt')],
           loc='upper center', bbox_to_anchor=(0.5, 0.87), ncol=2, frameon=False, fontsize=11)
for ax, phase, title, xlimit in zip(axes, SCENARIOS, SCENARIO_NAMES, (400, 160)):
    for kernel in ('normal', 'rt'):
        item = runs[kernel][phase]
        counts = np.zeros(max(item['histogram']) + 1, dtype=np.int64)
        for latency, count in item['histogram'].items():
            counts[latency] = count
        exceed = (item['stats']['cycles'] - np.cumsum(counts)) / item['stats']['cycles'] * 100
        positive = exceed > 0
        ax.step(np.arange(len(counts))[positive], exceed[positive], where='post',
                color=COLORS[kernel], linewidth=2, label=LABELS[kernel])
    ax.set_yscale('log')
    ax.set_xlim(0, xlimit)
    ax.set_ylim(0.0001, 150)
    ax.set_yticks([100, 10, 1, 0.1, 0.01, 0.001, 0.0001])
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, position: f'{value:g}'))
    ax.grid(which='major', color='#E5EAF0', linewidth=0.8)
    ax.set_title(title, loc='left', fontsize=13, weight='bold', pad=15)
    ax.set_xlabel('延迟阈值 / μs')
    ax.set_ylabel('超过阈值的样本 / %')
fig.text(0.08, 0.085, '例如横轴 100 μs 处的纵轴，表示本轮唤醒延迟超过 100 μs 的样本比例。', fontsize=10, color='#526373')
fig.text(0.08, 0.035, '这是样本分布，不是时间序列；未从直方图推测尖峰出现的时刻。', fontsize=10, color='#526373')
save_figure(fig, '20261022_rt_latency_tail_01')

outputs = [csv_path] + [PHOTOS / (name + suffix)
           for name in ('20261022_rt_latency_comparison_01', '20261022_rt_latency_tail_01')
           for suffix in ('.png', '.svg')]
manifest = {
    'sources': {name: digest((DATA / name).read_bytes()) for name in archives.values()},
    'calculations': {'mean': 'cycle-weighted mean of reported per-thread averages, rounded to 0.01 us',
                     'p999': 'smallest integer latency with cumulative count >= ceil(0.999 * total cycles)',
                     'tail': '100 * count(recorded latency > integer threshold) / total cycles'},
    'outputs': {p.relative_to(PROJECT).as_posix(): digest(p.read_bytes()) for p in outputs},
    'input_results_unchanged': True,
}
(DATA / 'plot_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for row in rows:
    print(row)
print('FIGURES_FROM_VERIFIED_RAW_DATA=PASS')
