#!/usr/bin/env python3
"""Paired GPU timing of M1 versus M2 in one EGL device context; use flock."""
import pathlib,subprocess,tempfile,sys,json
root=pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='hyprune-m2-bench-') as directory:
 temp=pathlib.Path(directory)
 for source,dest in [('Renderer.cpp','Legacy.cpp'),('Renderer.hpp','Legacy.hpp'),('GLState.hpp','LegacyGLState.hpp')]:
  text=subprocess.check_output(['git','show',f'origin/main:src/render/{source}'],cwd=root,text=True)
  text=text.replace('hyprune::render','hyprune::legacy').replace('"Renderer.hpp"','"Legacy.hpp"').replace('"GLState.hpp"','"LegacyGLState.hpp"')
  (temp/dest).write_text(text)
 command=['c++','-std=c++23','-O2','-DHYPRUNE_BENCH_LEGACY',f'-I{temp}','-Isrc','-Ithird_party','tests/material-benchmark.cpp','src/render/Renderer.cpp',str(temp/'Legacy.cpp'),'build/libhyprune_runtime.a','-lEGL','-lGLESv2','-pthread',*subprocess.check_output(['pkg-config','--libs','libxml-2.0','openssl'],text=True).split(),'-o',str(temp/'bench')]
 subprocess.run(command,cwd=root,check=True)
 report=json.loads(subprocess.check_output([str(temp/'bench'),'tests/fixtures/m2',*sys.argv[1:]],cwd=root,text=True))
 report['baseline']='M1 renderer '+subprocess.check_output(['git','rev-parse','origin/main'],cwd=root,text=True).strip()
 report['m1']=report.pop('diffuse');report['m2']=report.pop('materials')
 report['m2Quality']='intel' if 'Intel' in report['renderer'] else 'high'
 report['m1Msaa']=4;report['m2Msaa']=2 if report['m2Quality']=='intel' else 4
 report['m2ProbeResolution']=64 if report['m2Quality']=='intel' else 128
 report['m2AnisotropyMax']=2 if report['m2Quality']=='intel' else 8
 report['bloomResolution']='half';report['ssr']=False
 report.pop('msaa',None);report.pop('anisotropyMax',None)
 print(json.dumps(report,indent=2))
