#!/usr/bin/env python3
"""Paired historical/current PTL ink timings + matched captures; never contacts Hyprland.
Usage: flock /tmp/worldshell-nested.lock python3 dev/benchmark-ink.py WORLD [WORLD ...] OUTPUT_DIR
The caller must hold the nested lock for the whole run. Only Intel EGL is selected.
"""
import pathlib,subprocess,tempfile,sys,os
root=pathlib.Path(__file__).resolve().parent.parent
current_only="--current" in sys.argv
if current_only: sys.argv.remove("--current")
with tempfile.TemporaryDirectory(prefix='hyprune-ink-bench-') as directory:
 temp=pathlib.Path(directory)
 if not current_only: (temp/'Before.cpp').write_bytes(subprocess.check_output(['git','show','2494c13:src/render/Renderer.cpp'],cwd=root))
 flags=subprocess.check_output(['pkg-config','--cflags','--libs','freetype2','fontconfig','xkbcommon','libpng','openssl','libxml-2.0'],text=True).split()
 for label,source in ([('after','src/render/Renderer.cpp')] if current_only else [('before',str(temp/'Before.cpp')),('after','src/render/Renderer.cpp')]):
  command=['c++','-std=c++23','-O2','-Isrc','-Isrc/render','-Ibuild/generated','-Ithird_party','tests/ink-benchmark.cpp',source if label=='before' else 'build/CMakeFiles/material-benchmark.dir/src/render/Renderer.cpp.o','build/libhyprune_runtime.a','-lEGL','-lGLESv2','-pthread',*flags,'-o',str(temp/label)]
  subprocess.run(command,cwd=root,check=True)
  environment=os.environ.copy();environment['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/50_mesa.json'
  environment['XDG_CACHE_HOME']=str(temp/'cache'/label)
  for world in sys.argv[1:-1]:
   subprocess.run([str(temp/label),world,str(pathlib.Path(sys.argv[-1])/pathlib.Path(world).name/label)],cwd=root,env=environment,check=True)
