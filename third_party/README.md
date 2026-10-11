Vendored source, unmodified, licenses embedded in each header or kept beside it:

- cgltf v1.15, https://github.com/jkuhlmann/cgltf/tree/v1.15 (MIT).
- nlohmann/json v3.12.0, https://github.com/nlohmann/json/tree/v3.12.0 (MIT). The
  header carries only SPDX tags, so upstream `LICENSE.MIT` is kept as `json.LICENSE.MIT`.
- stb_image, commit f0569113c93ad095470c54bf34a17b36646bbbb5,
  https://github.com/nothings/stb (MIT/public domain).
- meshoptimizer v0.25 (tag commit 6daea4695c48338363b08022d2fb15deaef6ac09),
  https://github.com/zeux/meshoptimizer/tree/v0.25 (MIT, `meshoptimizer/LICENSE.md`).
  Runtime subset of `src/` only: allocator, index generator, vertex cache /
  fetch / overdraw optimizers, vertex/index codecs, vertex filters, quantization.
  Release tarball SHA-256 `68b2fef4e4eaad98e00c657c1e7f8982a7176e61dd7efdeaec67a025b8519be9`.
- AMD FidelityFX FSR 1, `ffx_a.h` and `ffx_fsr1.h`, commit
  a21ffb8f6c13233ba336352bdff293894c706575,
  https://github.com/GPUOpen-Effects/FidelityFX-FSR (MIT, `fidelityfx-fsr/license.txt`).
  Embedded into the EASU/RCAS shaders at configure time (`src/render/Fsr.hpp.in`).
- Basis Universal v2.1.0 transcoder (tag commit 45d5f41015eecd9570d5a3f89ab9cc0037a25063),
  https://github.com/BinomialLLC/basis_universal (Apache-2.0, `basisu/LICENSE`, `basisu/NOTICE`):
  `transcoder/` unmodified, plus its bundled single-file zstd decoder `zstd/zstddeclib.c`
  (BSD-3-Clause, `basisu/zstd/LICENSE`). KTX2 (KHR_texture_basisu) transcoding only; no encoder.
  Release tarball SHA-256 `ee1dbeb4c16699b577a0c78dce337bbede268e04bd2d463946971f8cb1e9c8df`.

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

World format 0.7's schema is vendored from schema `6250450` under
`hyprune-schema/v0.7` (`world.schema.json`, `common.schema.json`), byte-for-byte;
`WORLD-SOURCE.json` records the commit and SHA-256s and CTest `schema-pin`
verifies them and that `src/world/SchemaData.hpp` is regenerated.
