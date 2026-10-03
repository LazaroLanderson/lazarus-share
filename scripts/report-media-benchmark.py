#!/usr/bin/env python3
"""Summarize complete matched repetitions without inventing unavailable counters."""
import argparse
import collections
import json
from pathlib import Path
import statistics

parser=argparse.ArgumentParser()
parser.add_argument('input')
parser.add_argument('--output',default='dist/benchmarks/comparison.md')
parser.add_argument('--allow-partial',action='store_true')
args=parser.parse_args()
rows=[json.loads(line) for line in Path(args.input).read_text().splitlines()]
groups=collections.defaultdict(dict)
for row in rows:
    if row.get('window_aligned') and not row['exit_code']:
        key=(row['preset'],row['viewers'],row['requested_backend'])
        groups[key][(row['version'],row['repeat'])]=row

def aggregate(rows):
    median=lambda function:statistics.median(function(row) for row in rows)
    return {'cpu':median(lambda r:r['host']['cpu_pct']),
            'rss':median(lambda r:r['host']['rss_mib']),
            'clients_cpu':median(lambda r:sum(c['cpu_pct'] for c in r['clients'])),
            'capture':median(lambda r:r['captured_fps']),
            'encoded':median(lambda r:min(s['encoded_fps'] for s in r['streams'])),
            'decoded':median(lambda r:min(s['decoded_fps'] for s in r['streams'])),
            'p95':median(lambda r:max(s['latency_p95_ms'] for s in r['streams'])),
            'rss_change':median(lambda r:r['host']['rss_change_mib']),
            'encoder':sorted({e for r in rows for s in r['streams'] for e in s['encoder']})}

lines=['## Comparação medida','',
       'Medianas de três repetições. FPS e latência usam o pior viewer de cada repetição; CPU dos viewers é apresentada separadamente. CPU de 100% equivale a um núcleo.', '',
       '| Preset | Viewers | Backend | CPU host 0.2.0 → 0.2.1 | Ganho CPU | FPS codificados | FPS decodificados | p95 apresentação | Gate |',
       '|---|---:|---|---|---:|---|---|---|---|']
failures=[];incomplete=[];hardware_unavailable=[]
for key,group in sorted(groups.items()):
    required=[(v,n) for v in ('baseline','current') for n in (1,2,3)]
    if any(r not in group for r in required):incomplete.append(key);continue
    baseline=aggregate([group[('baseline',n)] for n in (1,2,3)])
    current=aggregate([group[('current',n)] for n in (1,2,3)])
    comparable=baseline['encoder']==current['encoder']
    if key[2]=='hardware' and any(not group[r]['hardware_available'] for r in required):comparable=False;hardware_unavailable.append(key)
    gain=(1-current['cpu']/baseline['cpu'])*100
    fps_ok=current['encoded']>=baseline['encoded']*.95 and current['decoded']>=baseline['decoded']*.95
    latency_ok=current['p95']<=baseline['p95']*1.10
    status=('PASSOU' if fps_ok and latency_ok else 'INVESTIGAR') if comparable else 'não comparável'
    if status=='INVESTIGAR':failures.append(key)
    lines.append(f'| {key[0]} | {key[1]} | {key[2]} | {baseline["cpu"]:.1f}% → {current["cpu"]:.1f}% | {gain:+.1f}% | {baseline["encoded"]:.2f} → {current["encoded"]:.2f} | {baseline["decoded"]:.2f} → {current["decoded"]:.2f} | {baseline["p95"]:.0f} → {current["p95"]:.0f} ms | {status} |')
lines+=['','## Recursos por processo','', '| Preset | Viewers | Backend | CPU viewers 0.2.0 → 0.2.1 | RSS host 0.2.0 → 0.2.1 | Variação RSS host 0.2.1 na janela |', '|---|---:|---|---|---|---|']
for key,group in sorted(groups.items()):
    if len(group)!=6:continue
    before=aggregate([group[('baseline',n)] for n in (1,2,3)]);after=aggregate([group[('current',n)] for n in (1,2,3)])
    lines.append(f'| {key[0]} | {key[1]} | {key[2]} | {before["clients_cpu"]:.1f}% → {after["clients_cpu"]:.1f}% | {before["rss"]:.1f} → {after["rss"]:.1f} MiB | {after["rss_change"]:+.2f} MiB |')
lines+=['',f'Cenários completos: {sum(len(g)==6 for g in groups.values())}/18. Incompletos: {len(incomplete)}. Gates para investigar: {len(failures)}. Hardware indisponível: {len(hardware_unavailable)}.']
output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
print(json.dumps({'complete':sum(len(g)==6 for g in groups.values()),'expected':18,'incomplete':incomplete,'regressions':failures,'hardware_unavailable':hardware_unavailable}))

if failures or (sum(len(g)==6 for g in groups.values())!=18 and not args.allow_partial):raise SystemExit(2)
