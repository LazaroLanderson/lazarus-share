#!/usr/bin/env python3
"""Compare isolated host/viewer processes. Synthetic video only; no raw signaling logs."""
import argparse
import itertools
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
import threading
import hashlib

p=argparse.ArgumentParser()
p.add_argument('--baseline',required=True)
p.add_argument('--current',default='build/media-benchmark')
p.add_argument('--output',default='dist/benchmarks/0.2.1.jsonl')
p.add_argument('--warmup',type=int,default=15)
p.add_argument('--duration',type=int,default=60)
p.add_argument('--repeats',type=int,default=3)
p.add_argument('--native-width',type=int,default=1920)
p.add_argument('--native-height',type=int,default=1080)
p.add_argument('--viewers',default='1,2,4')
p.add_argument('--presets',default='low,high,native')
p.add_argument('--backends',default='software,hardware')
a=p.parse_args()
output=Path(a.output);output.parent.mkdir(parents=True,exist_ok=True)
# Root-relative paths, SDP, credentials and contents of frames never enter results.
def process(pid):
    try:
        fields=Path(f'/proc/{pid}/stat').read_text().split(') ',1)[1].split()
        rss=int(Path(f'/proc/{pid}/statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE')
        busy={};seen=set()
        for fd in Path(f'/proc/{pid}/fdinfo').iterdir():
            try:
                values=dict(line.split(':',1) for line in fd.read_text().splitlines() if ':' in line)
                client=values.get('drm-client-id')
                if not client or client in seen:continue
                seen.add(client)
                for key,value in values.items():
                    if key.startswith('drm-engine-') and value.strip().endswith(' ns'):
                        busy[key.removeprefix('drm-engine-')]=busy.get(key.removeprefix('drm-engine-'),0)+int(value.strip().split()[0])
            except (OSError,ValueError):pass
        return (int(fields[11])+int(fields[12]),rss,busy,time.monotonic())
    except (OSError,ValueError):return None

def summarize(rows):
    if not rows:return None
    keys=set().union(*(r.keys() for r in rows))
    return {key:statistics.mean([r[key] for r in rows if r.get(key) is not None]) for key in keys if any(r.get(key) is not None for r in rows)}
def percentile(values,fraction):
    values=sorted(values);return values[min(len(values)-1,int(math.ceil(len(values)*fraction))-1)] if values else None

def run(binary,preset,count,backend,repeat):
    width,height,fps,kbps={'low':(1280,720,30,3000),'high':(1920,1080,60,8000),'native':(a.native_width&~1,a.native_height&~1,60,min(40000,max(8000,int(8000*a.native_width*a.native_height/(1920*1080)))))}[preset]
    env=os.environ|{'GST_DEBUG':'0','QT_QPA_PLATFORM':'offscreen','LAZARUS_RENDERER':'software'}
    if backend=='software':env['LAZARUS_VIDEO_ENCODER']='vp8'
    else:env.pop('LAZARUS_VIDEO_ENCODER',None)
    command=[str(Path(binary).resolve()),f'--width={width}',f'--height={height}',f'--fps={fps}',f'--kbps={kbps}',f'--viewers={count}',f'--seconds={a.warmup+a.duration+2}']
    child=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    watchdog=threading.Timer(a.warmup+a.duration+60,child.kill);watchdog.start()
    records=[];stats=[];pids=[];previous={};latency=[[] for _ in range(count)];readers=[[] for _ in range(count+1)]
    try:
        for line in child.stdout:
            value=json.loads(line)
            if value.get('event')=='processes':
                pids=[int(value['host_pid'])]+[int(x) for x in value['viewers']]
                previous={pid:process(pid) for pid in pids};continue
            if value.get('event')!='sample':continue
            previous_ms=records[-1]['elapsed_ms'] if records else 0
            value['capture_fps']=value['captured']*1000/max(1,value['elapsed_ms']-previous_ms);records.append(value)
            measuring=a.warmup*1000<=value['elapsed_ms']<(a.warmup+a.duration)*1000
            for index,pid in enumerate(pids):
                current=process(pid);old=previous.get(pid);previous[pid]=current
                if measuring and current and old:
                    ticks,rss,gpu,now=current;elapsed=now-old[3]
                    sample={'cpu_pct':(ticks-old[0])/os.sysconf('SC_CLK_TCK')/elapsed*100,'rss_mib':rss/1048576}
                    for engine,ns in gpu.items():
                        if engine in old[2]:sample['gpu_'+engine+'_time_pct']=max(0,ns-old[2][engine])/(elapsed*1e9)*100
                    readers[index].append(sample)
            if measuring:
                stats.append(value)
                for index,connection in enumerate(value['connections']):latency[index].extend(connection['presentation_ms'])
        code=child.wait(timeout=10)
    finally:
        watchdog.cancel()
        if child.poll() is None:child.terminate();child.wait(timeout=5)
    result={'preset':preset,'viewers':count,'requested_backend':backend,'repeat':repeat,'version':'baseline' if binary==a.baseline else 'current','width':width,'height':height,'target_fps':fps,'target_kbps':kbps,'warmup_s':a.warmup,'measurement_s':a.duration,'exit_code':code,'renderer':'software','host':summarize(readers[0]) if readers else None,'clients':[summarize(r) for r in readers[1:]],'gpu_counter_source':'DRM fdinfo when available'}
    streams=[]
    for index in range(count):
        values=[sample['connections'][index] for sample in stats]
        encoders={v['encoded'].get('encoder','') for v in values}
        streams.append({'encoder':sorted(encoders),'encoded_fps':statistics.mean([v['encoded'].get('video_fps',0) for v in values]) if values else 0,'decoded_fps':statistics.mean([v['decoded'].get('video_fps',0) for v in values]) if values else 0,'video_kbps':statistics.mean([v['encoded'].get('kbps',0) for v in values]) if values else 0,'discarded_frames_per_sample':statistics.mean([v['encoded'].get('frames_discarded',0) for v in values]) if values else None,'latency_p50_ms':percentile(latency[index],.5),'latency_p95_ms':percentile(latency[index],.95),'identified_frames':len(latency[index])})
    result['binary_sha256']=hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    result['captured_fps']=statistics.mean([s['capture_fps'] for s in stats]) if stats else 0
    result['streams']=streams
    result['hardware_available']=any('GPU' in e or 'NVENC' in e or 'Quick Sync' in e for stream in streams for e in stream['encoder']) if backend=='hardware' else None
    return result

existing=set()
if output.exists():
    for line in output.read_text().splitlines():
        r=json.loads(line);existing.add((r['preset'],r['viewers'],r['requested_backend'],r['repeat'],r['version']))
scenarios=list(itertools.product(a.presets.split(','),map(int,a.viewers.split(',')),a.backends.split(','),range(1,a.repeats+1)))
for index,(preset,count,backend,repeat) in enumerate(scenarios,1):
    # Alternate versions so thermal/time-of-day effects do not align with one version.
    versions=[a.baseline,a.current] if repeat%2 else [a.current,a.baseline]
    for binary in versions:
        version='baseline' if binary==a.baseline else 'current'
        if (preset,count,backend,repeat,version) in existing:continue
        result=run(binary,preset,count,backend,repeat)
        with output.open('a') as handle:handle.write(json.dumps(result,separators=(',',':'))+'\n')
        print(json.dumps({'completed_pair':index,'total_pairs':len(scenarios),'version':version,'preset':preset,'viewers':count,'backend':backend,'repeat':repeat,'exit_code':result['exit_code']},separators=(',',':')),flush=True)
        if result['exit_code'] or any(not s['identified_frames'] for s in result['streams']):raise SystemExit('Benchmark failed or presentation marker was not decoded')
