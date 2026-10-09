# Photo camera, ink and authored visibility

IPC 0.13 and world format 0.5 add optional authoring and presentation features.
Normal gameplay cameras, older worlds and normal shell grants remain supported.

## Photo camera

Dev tooling may receive a one-use launch grant containing `camera.author`.
Normal shell grants must omit it. Request IPC 0.13, then call `camera.photo`:

```json
{"position":[0,2,4],"orientation":[0,0,0.70710678,0.70710678],"fov":20,"lensShift":[0.2,-0.1],"renderScale":1}
```

- Position is metres; orientation is a normalized `[x,y,z,w]` quaternion. Roll
  is preserved, including vertical camera directions. Reported roll is radians
  about local +Z; heading still uses core's +yaw-right convention.
- `fov` is vertical degrees, 5–140. Optional `lensShift` defaults to `[0,0]` and
  is the optical centre offset in NDC: +X right, +Y up, each within −2..2.
  The optical axis projects to `((1+x)/2,(1-y)/2)` in top-left screen coordinates.
- `renderScale` is fixed within 0.25–1, overriding dynamic resolution for this
  hold. It changes world resolution; app surfaces, ink and HUD stay native.
- Hold requires active world control, outside use/focus or replay. It finishes a
  carry, freezes locomotion/look/scope and suppresses gameplay actions. Debug,
  capture, record-toggle and world exit still work. No new cursor show/hide transition occurs.
- The owning connection can update the hold. Another author cannot replace or
  release it. `camera.release {}` restores the prior pose/lens. Disconnect and
  world exit release it; world replacement starts the new world's normal camera.
  Competing `camera.set`, `camera.fov` and travel/autodrive writes are rejected.
- `camera.get {}` reports the actual `position`, quaternion, `roll`, `fov`,
  `lensShift`, `held`, and held `renderScale` (null outside a hold).
- U captures the rendered pose/lens and a PNG using core's existing deferred FBO
  readback. Recording camera frames retain roll/shift; camera replay interpolates
  them, including wrapped roll. For tours with authored camera holds use camera
  replay, since camera IPC edits are not input events. Old recordings default
  missing roll/shift to zero. Recording privacy rules are unchanged.

While held, the in-process renderer supplies the exact requested camera rather
than using a delayed offload frame. Offload resumes when released. No world reload
or compositor reload is involved.

## World 0.5 ink style

Add an optional `outline` object to a world's complete `atmosphere` profile:

```json
{"enabled":true,"colour":[0.02,0.025,0.04],"widthPx":2,"depthThreshold":0.02,"normalThreshold":0.2}
```

All six fields are required if the object is present. `colour` is display RGB
(0–1); `widthPx` is 1–4 logical pixels. `depthThreshold` is a relative linear-depth
jump (0.0001–1), and `normalThreshold` is `1-dot(n1,n2)` (0.001–2).

Core draws opaque/masked world geometry into native-resolution normal/depth
textures, detects discontinuities, and composites ink before transparent world
geometry, live app/screens and HUD. App buffers never feed the detector. Their
premultiplied alpha naturally reveals the outlined world behind them. Blend/haze
geometry does not generate ink. Material-ID edges are not implemented.
Widths are independent of distance and internal render scale, scaled by the
output's physical/logical ratio. Geometry normals are used, not normal maps.

`graphics.outline` is null (inherit preset), false (disable), or true (permit the
world's style). Presets permit authored ink; worlds without enabled ink pay no
geometry/fullscreen ink cost. Disabling ink does not unload the world. Programs
and blend variants are prepared behind the loading state and use the binary cache.
F3 and `graphics.get` expose its independent `outline` GPU timer, including the
normal/depth pass. This is an optional effect with a measurable native-4K cost.

## World 0.5 visibility

Visibility regions are independent of workspace homes:

```json
{"visibility":{"areas":[
  {"id":"upper","bounds":{"min":[-10,0,-10],"max":[10,6,10]},"nodes":[2],"visibleFrom":["lower"]},
  {"id":"lower","bounds":{"min":[-10,-6,-10],"max":[10,0,10]},"nodes":[8],"visibleFrom":[]}
]}}
```

Node indices designate glTF subtrees. Each subtree has one owner; conflicting
nested assignments are rejected. A region is visible from itself and each named
source in its `visibleFrom` list. Links are directed and not transitive. Overlap
unions all containing regions (useful at doors). Boundaries are inclusive.
Outside all regions, all geometry is visible. Unassigned geometry stays visible.

Authors must include rooms visible through doors/windows; this is a conservative
PVS declaration, not an automatic portal solver. Culling affects all world render
passes, including depth, ink and glass. It never removes collision, navigation,
live apps, workspace surfaces or HUD. Editor-transformed nodes draw conservatively.
`graphics.visibility_culling` toggles submission filtering without reload; F3 and
`timings.visibility` report submitted/culled/total batch counts.

## Validation

Run `HYPRUNE_NESTED_4K=1 dev/nested/run.sh --authoring`; the script holds
`flock /tmp/worldshell-nested.lock`. It only controls its verified nested instance.
The generic visibility fixture is built from `tests/fixtures/visibility.py`.
`docs/evidence/authoring.json` records Intel timings and checks. Public release
worlds without region metadata are explicitly measured as unannotated controls.

After ladder dismount, a short “Step back; climb grip resetting” prompt explains
the automatic re-grab delay. Existing explicit auto-traverse E remains immediate.

### Measured results

Intel PTL, nested 3840×2160, Low preset, fixed 0.5 world scale, 24 timing samples
per case; ink stays native resolution. Median ink pass costs: Lumen Reach
**7.51 ms**, Switchyard **8.22 ms**. Combined core GPU passes with ink were
15.87/16.22 ms respectively; these exclude the host compositor and are not a
sustained 60 FPS claim. The effect is optional and independently switchable.

The visibility laboratory submits **17 rather than 401** batches (384 culled).
See the evidence JSON for paired run timings. CPU submission falls from **0.776 to 0.115 ms** (85%);
GPU timings vary with clocks and host activity, so no GPU speedup is claimed.
Lumen Reach (87 batches) and Switchyard (134) currently have no visibility metadata:
all batches are retained and there is **no culling gain** until worlds opt in.

Validation: 13 core test groups; 100 schema tests; locked 4K nested authoring
checks, including actual camera capture and protocol response validation. 58 SDK tests plus both optional isolated hypruned integration tests pass;
all 49 locked nested climbing checks pass. No real-session
action was issued.
