# Gizmo 2.0 evidence

- `nested-gizmo.json`: `dev/nested/run.sh --gizmo` checks (Lumen Reach 0.10.1, nested only).
- `before-after.png`: the M3B gizmo (origin/main build) and Gizmo 2.0 on the same placed
  window from the same three cameras (front, high three-quarter, level: edge-on ring and
  end-on axis), Gizmo 2.0 on the Sun Court world object, and the M3B evidence captures.
  Stills: `GIZMO_STAGE=sheet [HYPRUNE_NESTED_PLUGIN=<other build>] dev/nested/run.sh --gizmo`.
- `refused.png`: a drag into the pedestal: the object keeps its start pose, the red ghost
  and the BLOCKED readout show the refused pose (from `GIZMO_STAGE=demo`).
- `polish-before-after.png`: halo, stroke width and size polish, before (main 51c98c3)
  and after on the Sun Court object and a window from the same three cameras, with a
  1:1 detail.
