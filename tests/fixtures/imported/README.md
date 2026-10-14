Objects written by the SDK's `hyprune object import` from its CC0 fixtures
(sdk test/fixtures/import, Hyprune contributors): `barrel` (glTF, centimetres, Z-up; decimated,
512 px KTX2 texture, convex proxy, CC0-1.0 recorded) and `crate` (OBJ through Assimp; nothing
recorded, so LicenseRef-unknown). Regenerate with:

    HYPRUNE_OBJECT_DIR=$PWD/tests/fixtures/imported hyprune object import <sdk>/test/fixtures/import/barrel/barrel.gltf \
      --up z --max-triangles 2000 --max-texture 512 --ktx2 all --license CC0-1.0 --author "Hyprune contributors" \
      --source https://github.com/hyprune/sdk --replace
    HYPRUNE_OBJECT_DIR=$PWD/tests/fixtures/imported hyprune object import <sdk>/test/fixtures/import/crate/crate.obj --replace
