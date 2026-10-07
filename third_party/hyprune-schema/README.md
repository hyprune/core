Pinned from sibling `hyprune/schema` commit 96dad32 (2026-10-07), v0 world/common
contracts. Source of truth: https://github.com/hyprune/schema (origin of sibling
repository). The generated `src/world/SchemaData.hpp` embeds these exact bytes so
runtime validation does not depend on network access or a sibling checkout.

Update both JSON files and regenerate the raw string constants together when the
contract changes. The validator implements only the keywords these files use;
new keywords require code and conformance tests before updating.
