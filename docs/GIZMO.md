# Position tool: Gizmo 2.0

Tool 4 (Position) edits windows, collision-owning scene nodes and, when enabled, world
format 0.8 placed objects. This page describes the handles, the input model, the
placement rules applied to edits and what is still outside the IPC contract.

## Handles

Handles are real 3D geometry projected every frame at a constant screen size (184
logical pixels per axis, scaled by the output scale) and drawn by core at native output
resolution after tone mapping: anti-aliased strokes (3 to 4 logical pixels) and shaded
fills in painter's order (`src/interaction/Gizmo.*`, drawn by `Renderer::drawGizmo`).
Every stroke and opaque fill sits on a dark ink halo (1.75 logical pixels each side),
drawn in a first pass so a halo never covers another handle's colour; handles stay
readable over busy, bright or same-hued surfaces. Axis colours follow the
HUD palette: coral X, mint Y, sky Z; neutral for screen-space handles, gold for hover
readouts.

| Mode | Handles |
| --- | --- |
| Move | an arrow (shaft and shaded cone) per axis; a plane square per axis pair (coloured by the plane's normal); a centre disc that moves in the screen plane |
| Rotate | a full circle per axis (the camera-facing half solid, the back half faint and dashed); an outer view ring (rotation about the view axis); inside the sphere, free trackball rotation |
| Scale | a line and shaded box per axis; plane squares for two-axis scale; a centre cube for uniform scale. Selections that only scale uniformly (world objects) show the cube with a ring that scales by distance from the centre |
| Universal | arrows, plane squares, rings outside the arrows and the scale boxes on the arrow shafts with the centre cube, all at once (enabled with `HYPRUNE_GIZMO_UNIVERSAL=1`) |

The wheel (tool Cycle) steps through move, rotate, scale and, when enabled, universal.

Degenerate views never leave a handle that can't be grabbed:

- Axis arrows and boxes point to the camera's side of the selection (with hysteresis,
  frozen during a drag). An axis within about 9 degrees of the view direction is
  disabled and fades back in by about 20 degrees.
- Plane squares fade out and are disabled as their plane turns edge-on.
- A ring seen edge-on is a line on screen. It stays pickable on its outer parts (not
  near the centre, where the trackball and centre handles win) and drags along its
  screen tangent; other rings drag by the angle swept in their plane, accumulated past
  half a turn.

Parts behind world geometry are dimmed and dashed. The selection's own extent around
the pivot is excluded, so the object being edited never hides its handles.

## Feedback

- Hover brightens and thickens the handle under the reticle and tightens the reticle;
  a small label names the handle with the current space and pivot.
- During a drag the active handle stays bright and the others dim. Axis drags show an
  infinite guide line; rotations show the swept wedge with start and current spokes.
- A readout beside the pointer shows the delta per axis in metres, the angle in degrees
  or the scale factor; the placement verdict is on a second line.
- Shift (or the snapping toggle, tool Alternate) snaps to 0.1 m, 15 degrees and 0.1x
  and draws tick marks along the guide or around the ring.

## Input

| Input | Effect |
| --- | --- |
| Primary on a handle, drag | edit; release commits (objects) or keeps (windows) |
| Primary elsewhere | select the window or object under the reticle |
| Secondary | world / local axes (scale always uses the selection's own axes) |
| Alternate | snapping on / off |
| Cycle (wheel) | move / rotate / scale (/ universal) |
| Digits, `.`, `-`, Backspace during a drag | type an exact value (metres, degrees or factor) for single-axis, ring and uniform handles; the button can be released; Enter applies, Escape cancels |
| Escape during a drag | cancel, back to the start pose |
| Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y | undo, redo (64 edits, objects and windows) |
| period | pivot at the centre or at the base (bottom centre; a window's lower edge) |
| End, Shift+End | drop the selected object onto the surface below it; align it to the aimed surface |

The keys in the last four rows are handled by core while tool 4 is active in world
mode; a key that an action binds with the same modifiers keeps its binding. In tool 4
a Ctrl press's own actions (crouch, fly down) wait up to 180 ms: an undo/redo chord in
that window consumes Ctrl entirely; Ctrl held longer acts as usual. Elsewhere Ctrl goes down
at once, and a Ctrl chord (Ctrl+R) ends the descent and puts the height back.

## Targets

- **Windows:** as in M3B, moved live with a swept collision test (a refused step stops
  the window and outlines the refused pose); scale resizes the client.
- **Scene nodes** that own collision: the M3B per-world transform overlay.
- **World objects with the movable trait** (RFC-0011, IPC 0.22): user placements from
  the catalog, and authored objects of movable assets. Picked by swept sphere then the
  asset's rest-pose triangles at the instance pose (`object:<instance id>`, uniform
  scale within `movable.scale`); edits go to the per-world overlay (a placement record
  or an authored override). A moved instance's collision proxy, visibility regions and
  probe light follow its committed pose. Locked instances keep their handles and refuse
  edits. World 0.8 assets carry no traits, so their instances stay render-only;
  `HYPRUNE_GIZMO_OBJECTS=1` treats them as movable for development.
- **Characters** are never targets (the character trait is filtered at the pick).

Object drags and carries are previews: meshes follow the pointer, collision does not.
Release commits the pose (with an undo record) and only then rebuilds collision.

## Placement rules

Every object preview and commit (gizmo drag, Super carry, Super+Wheel push and rotate,
drop, align) is checked (`src/interaction/Placement.*`). The shape is the selection's
bounds at the candidate pose, shrunk by 2 cm for resting contact; with its own
collision, a bounds hit is confirmed against those triangles.

1. Inside the playable union (centre and support point) and above the respawn height.
2. Not intersecting static collision other than its own; not wholly inside solid
   geometry.
3. Not over a placed window (with its margin).
4. Not in a screen approach: the box in front of every anchor and tool mount, width
   plus 0.3 m each side, from 0.1 m above its floor to its top edge, 1.5 m deep; not in
   a `placementZones` box.
5. Collidable or taller than 0.3 m: not in spawn capsules (grown), climbables or
   keep-clear volumes.
6. Collidable: not intersecting the player's capsule.

A failing preview shows a red ghost (bounds and outline edges) at the candidate pose, a
BLOCKED readout and the invalid reticle; the object itself shows its start pose (gizmo)
or its last valid pose (carry), which is what release commits. A refused commit sends
`runtime.notice` `placement.blocked`. Rules 3 to 5 are waived for a selection whose
start pose already breaks them, so authored objects inside an approach can still be
edited; walls, the playable volume and the player always apply (`brokenSoftRules`).
The waiver is per edit: it is computed from the pose at the start of each drag, carry
or command, so an edit that clears the zone makes the rule apply to later edits. Support (rule 3) follows
the object's `movable.surfaces` and is never waived; for scene nodes and world 0.8
objects it stays advisory: the readout says when an edit leaves a previously supported
object floating.

## Development switches

| Variable | Effect |
| --- | --- |
| `HYPRUNE_GIZMO_OBJECTS=1` | world 0.8 placed objects count as movable (`surfaces: [any]`) |
| `HYPRUNE_GIZMO_UNIVERSAL=1` | the universal mode joins the wheel cycle (IPC 0.22 reports `universal`, older peers `move`) |
| `HYPRUNE_GIZMO_KEEPCLEAR=<file>` | keep-clear boxes `[{id, min, max}]` for rule 5 |

## Verification

- `tests/gizmo.cpp` (CTest `gizmo`): layout, constant size, DPI, picking including
  end-on axes, edge-on rings and the centre, flips and hysteresis, every drag type,
  snapping, numeric entry, modes, drawing, anchors, every placement rule, rule waivers
  and undo/redo.
- `dev/nested/run.sh --gizmo` (Lumen Reach 0.10 or later, `HYPRUNE_GIZMO_WORLD` or the
  worlds checkout's dist package; it stops at once when the package has no
  `sun-court-armillary` object; a placed window and that object):
  every mode, hover, snapping with ticks, plane and screen moves, undo/redo, numeric
  entry, Escape, the refused pose into the pedestal, all rules probed on world data,
  edge-on and end-on handles, local axes, the base pivot, drop to floor, uniform scale,
  universal mode, window move/scale/undo, and that Ctrl+Z / Ctrl+Y never fly the camera
  down while Ctrl alone still does. Evidence: `docs/evidence/gizmo/`.
- `dev/nested/run.sh --m3b` aims at the new handles through their anchors and checks the
  player rule on the laboratory's collision-owning cube.
- Hardware cursor transitions are on the supervised checklist in
  `dev/live/LIVE-TEST.md` ("Position tool: Gizmo 2.0").
