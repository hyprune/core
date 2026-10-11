#!/usr/bin/env python3
"""Embed the pinned offline world contracts without requiring sibling repositories."""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
base=root/'third_party/hyprune-schema'
entries=[('worldSchema','world.schema.json'),('commonSchema','common.schema.json'),('worldSchema02','v0.2/world.schema.json'),('commonSchema02','v0.2/common.schema.json'),('commonSchema03','v0.3/common.schema.json'),('worldSchema03','v0.3/world.schema.json'),('commonSchema04','v0.4/common.schema.json'),('worldSchema04','v0.4/world.schema.json'),('worldSchema05','v0.5/world.schema.json'),('worldSchema06','v0.6/world.schema.json'),('commonSchema06','v0.6/common.schema.json'),('worldSchema07','v0.7/world.schema.json'),('commonSchema07','v0.7/common.schema.json'),('worldSchema08','v0.8/world.schema.json'),('commonSchema08','v0.8/common.schema.json')]
text='#pragma once\nnamespace hyprune::world {\n'
for name,path in entries:
    text+=f'inline constexpr const char *{name} = R"schema({(base/path).read_text().rstrip()})schema";\n'
text+='} // namespace hyprune::world\n'
(root/'src/world/SchemaData.hpp').write_text(text)
