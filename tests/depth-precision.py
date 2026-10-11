"""Shaders that copy world depth into gl_FragDepth sample it through a highp sampler.

GLSL ES samplers default to lowp; NVIDIA then returns depth at fp16, so a window
wall a few centimetres in front of world geometry lost the depth test at 10 m.
"""
import pathlib,re
source=(pathlib.Path(__file__).resolve().parents[1]/'src/render/Renderer.cpp').read_text()
writes=list(re.finditer(r'gl_FragDepth\s*=\s*texelFetch\(\s*(\w+)',source))
assert len(writes)>=2,len(writes)
for write in writes:  # the nearest declaration before the write is the shader's own
    name=write.group(1)
    declared=[m for m in re.finditer(r'uniform\s+([^;"]*?)\b'+name+r'\b[^;"]*;',source[:write.start()])][-1]
    assert re.match(r'highp\s+sampler2D\b',declared.group(1)),declared.group(0)
print(f'PASS depth precision: {len(writes)} depth-writing shaders sample depth highp')
