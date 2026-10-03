#!/usr/bin/env python3
"""Summarize complete matched repetitions without inventing unavailable counters."""
import argparse
import collections
import json
import itertools
from pathlib import Path
import statistics

parser=argparse.ArgumentParser()
parser.add_argument('input')
parser.add_argument('--output',default='dist/benchmarks/comparison.md')
parser.add_argument('--allow-partial',action='store_true')
args=parser.parse_args()
rows=[json.loads(line) for line in Path(args.input).read_text().splitlines()]
groups=collections.defaultdict(dict)
hashes=collections.defaultdict(set)
invalid=0
for row in rows:
    usable=row.get('window_aligned') and not row['exit_code'] and row.get('warmup_s')==15 and row.get('measurement_s')==60 and abs(row.get('measured_elapsed_s',0)-60)<1 and all(s.get('identified_frames',0)>0 and s.get('latency_p95_ms') is not None for s in row['streams'])
    if usable:
        hashes[row['version']].add(row['binary_sha256'])
        key=(row['preset'],row['viewers'],row['requested_backend'])
        groups[key][(row['version'],row['repeat'])]=row
    else:invalid+=1
expected=set(itertools.product(('low','high','native'),(1,2,4),('software','hardware')))
mixed_hashes=any(len(values)!=1 for values in hashes.values())

def aggregate(rows):
    median=lambda function:statistics.median(function(row) for row in rows)
    return {'cpu':median(lambda r:r['host']['cpu_pct']),
            'rss':median(lambda r:r['host']['rss_mib']),
            'peak_rss':median(lambda r:r['host']['rss_peak_mib']),
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
lines+=['','## Contadores de GPU','',
        'Tempo de engine/contexto DRM do host; não é utilização global do chip. Viewers raster sem contexto DRM ficam indisponíveis.', '',
        '| Preset | Viewers | Backend | Engine | 0.2.0 → 0.2.1 |','|---|---:|---|---|---|']
for key,group in sorted(groups.items()):
    if len(group)!=6:continue
    counters=sorted({k for row in group.values() for k in row['host'] if k.startswith('gpu_')})
    if not counters:lines.append(f'| {key[0]} | {key[1]} | {key[2]} | — | indisponível |')
    for counter in counters:
        values=[]
        for version in ('baseline','current'):
            samples=[group[(version,n)]['host'].get(counter) for n in (1,2,3)]
            values.append(f'{statistics.median(samples):.1f}%' if all(v is not None for v in samples) else 'indisponível')
        lines.append(f'| {key[0]} | {key[1]} | {key[2]} | {counter.removeprefix("gpu_").removesuffix("_time_pct")} | {values[0]} → {values[1]} |')
lines+=['',f'Cenários completos: {sum(len(g)==6 for g in groups.values())}/18. Incompletos: {len(incomplete)}. Gates para investigar: {len(failures)}. Hardware indisponível: {len(hardware_unavailable)}.']
output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
print(json.dumps({'complete':sum(len(g)==6 for g in groups.values()),'expected':18,'incomplete':incomplete,'regressions':failures,'hardware_unavailable':hardware_unavailable}))

if mixed_hashes:raise SystemExit('Mixed binary hashes: keep each candidate in a separate dataset')
if failures or (not args.allow_partial and (invalid or set(groups)!=expected or any(len(g)!=6 for g in groups.values()))):raise SystemExit(2)
