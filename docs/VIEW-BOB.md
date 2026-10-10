# View bob: what others do, and Hyprune's default

The owner found Hypr3D's walk bob fine when walking but silly when running, and didn't
want a copy. This page is a short research pass (sources at the end) and the design that
came out of it. Code: `src/sim/ViewBob.hpp`; setting `player.view_bob` (IPC 0.19).

## What games and engines do

- **Vertical only, at a step or clock rate.** Doom bobs the view vertically, at most 8 map
  units at full speed, on a fixed 0.57 s cycle (about 1.75 Hz). The amplitude scales with
  speed squared [1]. Quake does the same: `cl_bob 0.02`, `cl_bobcycle 0.6` s (about 1.67 Hz),
  `cl_bobup 0.5`. Its strafe roll is a separate effect, `cl_rollangle 2.0` degrees [2].
- **The figure-8 sits on the weapon, not the eye.** Half-Life 2 has no camera bob. Its
  weapon bob has sideways motion at half the vertical frequency, and the bob advances
  with speed, so sprinting bobs faster and wider [3].
- **Engine starter controllers.** Unity's old first-person controller uses a ±10 cm
  figure-8 (vertical to horizontal ratio 2). That works out to about 2 Hz walking and
  3.4 Hz running, with the stride lengthened when running [4]. That is far stronger than
  is comfortable on a desktop screen.
- **Hypr3D** (read from its source): 3.5 cm vertical, 6 rad per metre walked, and a 0.46°
  roll at half the rate. Because the rate follows distance, its 10 m/s sprint bobs at about
  9.5 Hz with full amplitude. That is the "silly" part.
- **Games that remove or limit it:** Mirror's Edge removed head bob from an earlier build
  to fight simulator sickness, and added a centre dot as a fixation point [5]. Minecraft
  has an on/off "View Bobbing" option [6]. The Elder Scrolls Online has an intensity
  slider, which Microsoft cites as good practice [7].

## Comfort and accessibility guidance

- Game Accessibility Guidelines: avoid camera motion that differs from the player's own
  input, or provide an option to disable it. Head bob, weapon bob and camera shake are
  named explicitly [8].
- Xbox Accessibility Guideline 117: avoid repetitive up-and-down or side-to-side screen
  motion outside core gameplay, and offer off/reduced controls [7].
- Meta's VR locomotion guidance: limit acceleration and optic flow, and use real-world
  speeds (walk about 1.4 m/s, run about 2.8 m/s) [9].
- WCAG 2.3.3 says motion triggered by interaction can be disabled; it cites vestibular
  disorders. Operating systems expose this as "reduce motion" [10][11].
- Real gait: preferred walking cadence is about 2 steps/s, and the head's vertical motion
  peaks at the step frequency [12]. I didn't find a measured head amplitude in cm; the
  body's centre of mass moves up to about 5 cm at 6 km/h [13].

## Hyprune's default

| | Hyprune `subtle` (default) | `normal` | Hypr3D |
| --- | --- | --- | --- |
| Vertical | 1.0 cm dip per step | 2.2 cm dip | ±3.5 cm |
| Sideways | none | 0.66 cm at half the step rate | none (0.46° roll) |
| Roll | none | none | 0.46° |
| Step rate, walking | 1.9 Hz | 1.9 Hz | about 3.8 Hz at 4 m/s |
| Running | amplitude ×0.3, 2.5 Hz | same | full amplitude, about 9.5 Hz |
| Start/stop | eased over 0.18 s; starts from rest | same | eased |

- **One smooth dip per footstep:** `(1 - cos)/2`. The eye never rises above standing
  height, so it can't poke through a low ceiling. Starting from rest has zero offset and
  zero velocity, so there's no pop.
- **The step rate is decoupled from distance.** It stays near human walking, 1.9 Hz, and
  rises only to 2.5 Hz at run speed. Fast movement can't make it frantic.
- **Strongly reduced when running** (to 30%), not increased.
- **Only when grounded walking under player control.** It's off in flight and noclip, while
  climbing (climbing has its own subtle cue), while typing, during transitions, in photo
  mode and during scripted driving. It eases out rather than snapping.
- **Measured motion, not intent.** Walking into a wall doesn't bob, and teleports don't
  count as motion.
- **Settings:** off / subtle / normal. It's always off with `graphics.reduced_motion`. The
  bob is view-only: physics, picking, the reticle and recordings never see it.

Nested evidence (`dev/nested/run.sh --reticle`, `docs/evidence/reticle.json`):
- Walking at 4 m/s: the dip peaks at 0.68–0.98 cm and is never above eye height. It settles
  to exactly zero within 1.6 s of stopping.
- Running dips 0.42 cm. Normal dips 1.5 cm.
- Off, reduced motion and flight are exactly zero.

## Sources

1. Doom source, `p_user.c` (MAXBOB, 20-tic cycle): https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_user.c
2. Quake source, `view.c` (cl_bob, cl_bobcycle, cl_bobup, cl_rollangle): https://github.com/id-Software/Quake/blob/master/WinQuake/view.c
3. Source SDK 2013, `basehlcombatweapon_shared.cpp` and `hl2_player.cpp`: https://github.com/ValveSoftware/source-sdk-2013
4. Unity Standard Assets FPSController prefab and CurveControlledBob (mirror): https://github.com/Moolt/LevelGenerator/tree/master/Assets/Standard%20Assets
5. "How Mirror's Edge fights simulation sickness", Engadget 2008: https://www.engadget.com/2008/07/17/how-mirrors-edge-fights-simulation-sickness/
6. Minecraft Wiki, Options (community wiki): https://minecraft.wiki/w/Options
7. Xbox Accessibility Guideline 117: https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117
8. Game Accessibility Guidelines: https://gameaccessibilityguidelines.com/avoid-or-provide-option-to-disable-any-difference-between-controller-movement-and-camera-movement/
9. Meta, locomotion comfort and usability: https://developers.meta.com/horizon/design/locomotion-comfort-usability/
10. WCAG 2.2, Understanding 2.3.3 Animation from Interactions: https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html
11. MDN, `prefers-reduced-motion`: https://developer.mozilla.org/en-US/docs/Web/CSS/@media/prefers-reduced-motion
12. Woodman & Griffin, six axes of head acceleration during ambulation: https://www.ioa.org.uk/system/files/proceedings/pd_woodman_mj_griffin_six_axes_of_head_acceleration_during_ambulation.pdf
13. Centre-of-mass vertical displacement in walking (Acta of Bioengineering and Biomechanics 12(3), 2010): https://actabio.pwr.edu.pl/fcp/mGBUKOQtTKlQhbx08SlkTUARAUWRuHQwFDBoIVURNWHpSFVZpCFghUHcKVj0GQUw/302/public/publikacje/v12-3-2010/42.pdf
