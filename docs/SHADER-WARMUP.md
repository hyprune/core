# Shader preparation and binary cache

All eight GLES3 programs (world, native surfaces, depth, tone, upscale/sharpen,
bloom, sky and text) are prepared outside the compositor pass. Once resource
uploads finish, twelve small offscreen draws exercise MSAA, material/effect,
alpha/native, depth and debug/text paths, one fenced submission per loading tick.
The loading screen remains visible until warmup finishes. Performance changes
use these uniform-driven programs; they do not compile new source variants.

Program binaries use GLES3 `glGetProgramBinary`/`glProgramBinary` (the core
counterparts of ARB/OES program binary). Unsupported formats simply compile from
source. Cache entries live at `$XDG_CACHE_HOME/hyprune/programs` (default
`~/.cache/hyprune/programs`), with SHA-256 keys covering vendor, renderer, GL/GLSL
versions, build/compiler identity, renderer source hash and exact shader sources.
Writes are atomic, private files; bounded lengths and payload digests reject
truncation/corruption. A driver rejecting a binary falls back to source. The
sandboxed offload child receives access only to this cache subtree, not the rest
of the owner's home. Cold driver specialization can still take a long loading
tick; it is deliberately kept behind loading rather than on first interaction.

`graphics.get().timings.shaderCache` reports hits, misses, rejected binaries,
source compile time and warmup CPU time/maximum step. This is additive diagnostic
metadata, not a new effect or a required schema field.

Intel PTL, Lumen Reach, Mesa's own shader cache disabled, isolated cache roots,
all runs under `flock /tmp/worldshell-nested.lock`:

| Preparation | Source compile | Cache hits | Worst visible effect-switch submission |
| --- | ---: | ---: | ---: |
| No draw warmup | 58.68 ms | 0/8 | 26.40 ms |
| Warmed, cold binary cache | 57.71 ms | 0/8 | 0.90 ms |
| Warmed, binary cache from previous process | 0 ms | 8/8 | 0.57 ms |

The ~26 ms deferred driver specialization moved into the loading step. These are
CPU submission timings at 320×180 to isolate compilation, not total 4K GPU frame
budgets or a guarantee against allocation/OS stalls. The benchmark asserts zero
new program loads/compiles during fifteen subsequent effect/preset switches.
Full JSON measurements are in `docs/evidence/shaders/`. Software EGL tests also
exercise cache hits, corrupted-entry fallback and GL state restoration.

```sh
ctest --test-dir build --output-on-failure
# Set XDG_CACHE_HOME to separate temporary roots for cold and warm comparisons.
MESA_SHADER_CACHE_DISABLE=true flock /tmp/worldshell-nested.lock \
  build/shader-warmup-tests ../worlds/dist/com.hyprune.lumen-reach warm intel
```

The benchmark selects the Intel PCI vendor before creating its EGL context; it
does not start the NVIDIA renderer. No owner-session command is involved.
