Pinned from sibling `hyprune/schema` commit 96dad32 (2026-10-07), v0 world/common
contracts. Source of truth: https://github.com/hyprune/schema (origin of sibling
repository). The generated `src/world/SchemaData.hpp` embeds these exact bytes so
runtime validation does not depend on network access or a sibling checkout.

Update both JSON files and regenerate the raw string constants together when the
contract changes. The validator implements only the keywords these files use;
new keywords require code and conformance tests before updating.

IPC 0.3 reference bytes under `v0.3/` are pinned to schema commit
`4facaa1a66d01986e0d469762e9f93ac48858892`. Runtime world formats remain 0.1/0.2;
these IPC bytes support conformance tooling and do not change embedded world schemas.

M2 world 0.3 and IPC 0.4 bytes are pinned to `9dde810b2a817de2f1e775577d0551ab703da55d`.
Regenerate embedded world contracts with `python3 dev/embed-schema.py`.

M3A IPC 0.5 bytes are pinned to `2f8e80e458a930550034206da4458d857a1b1a4a`.
World contracts and earlier IPC bytes remain unchanged.

World 0.5 bytes are pinned to schema `2c030cb`. The world schema inlines its
optional atmosphere extension and otherwise reuses the frozen 0.4 common
constraints. Regenerated with `dev/embed-schema.py`; no runtime sibling access.

World 0.6 playable bounds/backdrop bytes are pinned to `2c1be9b3359025a0467eab7b0c11bc9aa339eeb5`;
its common schema reuses the already-pinned 0.6 bytes.

World 0.9 (characters, paths, seats, keepClear) and IPC 0.23 (NPC state and methods)
are pinned to the schema commit recorded in `v0.9/WORLD-SOURCE.json` and
`ipc-source.json`. The 0.9 world schema keeps its definitions in local `$defs`, so the
embedded validator resolves them against the world schema itself.

IPC 0.25 (companions, draft) and the object format 0.10 companion block are pinned to the
schema commit recorded in `ipc-source.json` and `OBJECT-SOURCE.json`.

IPC 0.26 (debug tool, companion pace, longer pocket list) is pinned to the schema commit
recorded in `ipc-source.json`.

Object format 0.11 (draft: the mount trait for workspace screens) and IPC 0.27 (workspace
screens) are pinned to the schema commit recorded in `OBJECT-SOURCE.json` and `ipc-source.json`.

`v0.11/import.schema.json` (import.json, importVersion 0.1) and the IPC 0.27 catalog fields are
pinned to the same commit.

World 0.11 (draft: panorama skies, object format 0.11 directory assets), `v0.11/project.schema.json`
(builder/project.json, projectVersion 0.1) and the IPC 0.27 builder state are pinned to the schema
commit recorded in `v0.11/WORLD-SOURCE.json`, `OBJECT-SOURCE.json` and `ipc-source.json`. World 0.11
follows 0.9: there is no world format 0.10.
