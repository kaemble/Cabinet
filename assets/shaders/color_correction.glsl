vec3 correct_color(vec3 color, vec4 adjustment, vec4 gain)
{
    color = (color - vec3(0.5)) * adjustment.y + vec3(0.5 + adjustment.x);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, adjustment.z) * gain.rgb;
    return pow(clamp(color, 0.0, 1.0), vec3(1.0 / adjustment.w));
}
