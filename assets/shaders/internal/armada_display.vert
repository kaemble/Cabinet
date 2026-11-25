#version 450
layout(push_constant) uniform Parameters {
    vec4 destination; // Physical center and unrotated half extents, in pixels.
    vec4 source;
    vec4 options; // Clockwise quarter turns, dim factor, source vertical flip, sharp factor.
    vec4 panel; // 2 / physical panel dimensions, source texture dimensions.
} p;
layout(location=0) out vec2 uv;
void main() {
    vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));
    vec2 c = corners[gl_VertexIndex];
    vec2 offset = c * p.destination.zw;
    int turns = int(p.options.x);
    if (turns == 1) offset = vec2(-offset.y, offset.x);
    if (turns == 2) offset = -offset;
    if (turns == 3) offset = vec2(offset.y, -offset.x);
    gl_Position = vec4((p.destination.xy + offset) * p.panel.xy - 1, 0, 1);
    vec2 coord = c * .5 + .5;
    if (p.options.z != 0) coord.y = 1 - coord.y;
    uv = p.source.xy + coord * p.source.zw;
}
