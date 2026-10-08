Vendored source, unmodified, licenses embedded in each header:

- cgltf v1.15, https://github.com/jkuhlmann/cgltf/tree/v1.15 (MIT).
- nlohmann/json v3.12.0, https://github.com/nlohmann/json/tree/v3.12.0 (MIT).
- stb_image, commit f0569113c93ad095470c54bf34a17b36646bbbb5,
  https://github.com/nothings/stb (MIT/public domain).

No prototype assets or source files are included.

`dev/nested/virtual-pointer.xml` is the upstream wlroots virtual-pointer protocol
from https://github.com/swaywm/wlr-protocols (retrieved 2026-10-07). Its embedded
MIT-style license is retained. It is used only by the nested integration test.
Protocol SHA-256: `3ff6d540be0bc5228195bf072bde42117ea17945a5c2061add5d3cf97d6bb524`.

Frozen world/common schemas under `hyprune-schema/v0.2` are from sibling schema commit `57bf330` (MIT); their manifest field shapes are unchanged by IPC-only follow-ups. The 0.1 copies remain intact. `src/world/SchemaData.hpp` embeds both for offline validation.

IPC/input schema 0.6 is vendored from schema commit `a1e2f84` under `hyprune-schema/v0.6` (MIT), byte-for-byte. Older protocol schema directories retain their frozen bytes.

World 0.4's schema is vendored from schema `569e92d` (feature/climbing, MIT).
Its common definitions are byte-identical to the already pinned 0.4 common
schema. `dev/embed-schema.py` embeds both for offline climbable validation.
