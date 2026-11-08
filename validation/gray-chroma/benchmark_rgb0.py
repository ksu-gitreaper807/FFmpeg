import subprocess, json, re, statistics
from pathlib import Path
runs=[]
for trial in range(7):
    order=['baseline','patched'] if trial%2==0 else ['patched','baseline']
    for build in order:
        r=subprocess.run(['taskset','-c','2',f'work/validate-{build}-asm','bench'],capture_output=True,text=True,check=True)
        rows=[]
        for line in r.stderr.splitlines():
            m=re.fullmatch(r'(\d+x\d+) rgb0 -> (\S+) ([\d.]+) ms/frame',line)
            if m: rows.append({'size':m[1],'format':m[2],'ms':float(m[3])})
        if len(rows)!=4: raise RuntimeError(r.stderr)
        runs.append({'trial':trial,'build':build,'measurements':rows})
summary=[]
for size in ['1920x1080','3840x2160']:
    for fmt in ['gray','yuv420p']:
        vals={b:[m['ms'] for r in runs if r['build']==b for m in r['measurements'] if m['size']==size and m['format']==fmt] for b in ['baseline','patched']}
        med={b:statistics.median(v) for b,v in vals.items()}
        summary.append({'size':size,'format':fmt,'median_ms':med,'time_reduction_percent':100*(1-med['patched']/med['baseline']),'samples':vals})
Path('outputs/benchmark-results.json').write_text(json.dumps({'cpu':'Intel Core i7-12800H','affinity_cpu':2,'method':'7 alternating baseline/patched runs; each case 5 warmup + 60 timed frames; SWS_BILINEAR; BT.709; limited output range; native CPU dispatch; single thread','runs':runs,'summary':summary},indent=2)+'\n')
print(json.dumps(summary,indent=2))
