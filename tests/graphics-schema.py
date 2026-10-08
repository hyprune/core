#!/usr/bin/env python3
"""Core-side proposed 0.7 contract: canonical publication belongs to schema owner."""
import json,pathlib
from jsonschema import Draft202012Validator
root=pathlib.Path(__file__).resolve().parents[1]
amend=json.loads((root/'docs/graphics-ipc-amendment.json').read_text())
schema={'$schema':'https://json-schema.org/draft/2020-12/schema','$defs':amend['$defs'],'$ref':'#/$defs/graphics.set.params'}
Draft202012Validator.check_schema(schema); validator=Draft202012Validator(schema)
for patch in [{'quality':'low'},{'msaa':None},{'render_scale':.5,'dynamic_resolution':False},{'sky':'gradient','normal_maps':True}]:validator.validate({'patch':patch})
for patch in [{'msaa':3},{'target_fps':0},{'quality':'best'},{'ssr':True},{'render_scale':True}]:assert not validator.is_valid({'patch':patch}),patch
print('PASS graphics 0.7 schema amendment positive/negative fixtures')
