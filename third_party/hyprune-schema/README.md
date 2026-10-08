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
