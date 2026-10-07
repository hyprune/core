#!/usr/bin/env python3
"""Compile historical M0 renderer beside M1; paired timings in one EGL context.
Run inside flock /tmp/worldshell-nested.lock. No compositor is contacted.
"""
import pathlib,subprocess,tempfile,sys
root=pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='hyprune-m0-bench-') as directory:
    temp=pathlib.Path(directory)
    for source,dest in [('Renderer.cpp','Legacy.cpp'),('Renderer.hpp','Legacy.hpp'),('GLState.hpp','LegacyGLState.hpp')]:
        text=subprocess.check_output(['git','show',f'6aa79e2:src/render/{source}'],cwd=root,text=True)
        text=text.replace('hyprune::render','hyprune::legacy').replace('"Renderer.hpp"','"Legacy.hpp"').replace('"GLState.hpp"','"LegacyGLState.hpp"')
        (temp/dest).write_text(text)
    command=['c++','-std=c++23','-O2','-DHYPRUNE_BENCH_LEGACY',f'-I{temp}','-Isrc','-Ithird_party','tests/material-benchmark.cpp','src/render/Renderer.cpp',str(temp/'Legacy.cpp'),'build/libhyprune_runtime.a','-lEGL','-lGLESv2','-pthread','-o',str(temp/'bench')]
    subprocess.run(command,cwd=root,check=True)
    subprocess.run([str(temp/'bench'),'tests/fixtures/materials',*sys.argv[1:]],cwd=root,check=True)
