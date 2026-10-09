#!/usr/bin/env python3
"""Summarise dev/nested/perf.py reports: median GPU ms per variant/pose/scale.
Usage: perf_compare.py REPORT.json [REPORT.json ...]; variants from different
reports are labelled report:variant."""
import json,statistics,sys
rows={}
for path in sys.argv[1:]:
    report=json.load(open(path))
    for item in report['poses']:
        key=(item['scale'],item['pose'])
        label=(report['label']+':' if len(sys.argv)>2 else '')+item.get('variant','A')
        rows.setdefault(key,{}).setdefault(label,[]).append(item)
passes=['world_shading_shared','upscale_sharpen','output_markers','native_surfaces_geometry','sky']
labels=sorted({l for v in rows.values() for l in v})
print('scale pose       variant      '+' '.join(f'{p[:10]:>10}' for p in passes)+'      total  n')
totals={l:[] for l in labels}
for (scale,pose),variants in sorted(rows.items()):
    for label in labels:
        items=variants.get(label,[])
        if not items:continue
        med=lambda f:statistics.median(f(i) for i in items)
        values=[med(lambda i:i['gpuMs'].get(p,0)) for p in passes];total=med(lambda i:i['gpuTotalMs'])
        totals[label].append(total)
        print(f'{scale:<5} {pose:<10} {label:<12} '+' '.join(f'{v:10.3f}' for v in values)+f' {total:10.3f} {len(items)}')
print('mean of pose medians: '+', '.join(f'{l} {statistics.mean(v):.3f} ms' for l,v in totals.items() if v))
