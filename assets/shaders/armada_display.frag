#version 450
layout(binding=0) uniform sampler2D frame;
layout(push_constant) uniform Parameters { vec4 destination; vec4 source; vec4 options; vec4 panel; } p;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
void main() {
    vec2 coord = uv;
    float factor = p.options.w;
    if (factor > 1) {
        // Equivalent to nearest integer enlargement followed by bilinear
        // filtering, with the crop's edges clamped like an intermediate texture.
        vec2 pixel = uv * p.panel.zw - .5;
        vec2 fraction = clamp(fract(pixel) * factor - .5 * (factor - 1), 0, 1);
        coord = (floor(pixel) + fraction + .5) / p.panel.zw;
        coord = clamp(coord, p.source.xy + .5 / p.panel.zw,
            p.source.xy + p.source.zw - .5 / p.panel.zw);
    }
    color = vec4(texture(frame, coord).rgb * p.options.y, 1);
}
