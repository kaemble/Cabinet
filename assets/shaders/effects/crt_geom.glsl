// cabinet-shader-begin
// {
//   "id": "crt_geom",
//   "name": "CRT-Geom",
//   "entry": "crt_geom",
//   "context": true,
//   "resolution": "output",
//   "parameters": [
//     {
//       "symbol": "crt_gamma",
//       "name": "CRT gamma",
//       "description": "Gamma of the simulated CRT",
//       "minimum": 1,
//       "maximum": 50,
//       "step": 1,
//       "initial": 24,
//       "scale": 0.1,
//       "suffix": "",
//       "display_scale": 0.1,
//       "decimals": 1
//     },
//     {
//       "symbol": "inverse_gamma",
//       "name": "Inverse gamma",
//       "description": "Compensate for the beam and mask gamma",
//       "minimum": 0,
//       "maximum": 1,
//       "step": 1,
//       "initial": 0,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "monitor_gamma",
//       "name": "Monitor gamma",
//       "description": "Gamma of the output display",
//       "minimum": 1,
//       "maximum": 50,
//       "step": 1,
//       "initial": 22,
//       "scale": 0.1,
//       "suffix": "",
//       "display_scale": 0.1,
//       "decimals": 1
//     },
//     {
//       "symbol": "view_distance",
//       "name": "Viewing distance",
//       "description": "Distance from the curved screen",
//       "minimum": 1,
//       "maximum": 30,
//       "step": 1,
//       "initial": 16,
//       "scale": 0.1,
//       "suffix": "",
//       "display_scale": 0.1,
//       "decimals": 1
//     },
//     {
//       "symbol": "curvature",
//       "name": "Curvature",
//       "description": "Enable curved screen geometry",
//       "minimum": 0,
//       "maximum": 1,
//       "step": 1,
//       "initial": 1,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "radius",
//       "name": "Curvature radius",
//       "description": "Radius of the simulated CRT surface",
//       "minimum": 1,
//       "maximum": 100,
//       "step": 1,
//       "initial": 20,
//       "scale": 0.1,
//       "suffix": "",
//       "display_scale": 0.1,
//       "decimals": 1
//     },
//     {
//       "symbol": "corner_size",
//       "name": "Corner size",
//       "description": "Size of the rounded screen corners",
//       "minimum": 1,
//       "maximum": 1000,
//       "step": 1,
//       "initial": 30,
//       "scale": 0.001,
//       "suffix": "",
//       "display_scale": 0.001,
//       "decimals": 3
//     },
//     {
//       "symbol": "corner_smoothness",
//       "name": "Corner smoothness",
//       "description": "Higher values produce a sharper screen border",
//       "minimum": 80,
//       "maximum": 2000,
//       "step": 20,
//       "initial": 1000,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "tilt_x",
//       "name": "Horizontal tilt",
//       "description": "Horizontal screen tilt in radians",
//       "minimum": -50,
//       "maximum": 50,
//       "step": 5,
//       "initial": 0,
//       "scale": 0.01,
//       "suffix": " rad",
//       "display_scale": 0.01,
//       "decimals": 2
//     },
//     {
//       "symbol": "tilt_y",
//       "name": "Vertical tilt",
//       "description": "Vertical screen tilt in radians",
//       "minimum": -50,
//       "maximum": 50,
//       "step": 5,
//       "initial": 0,
//       "scale": 0.01,
//       "suffix": " rad",
//       "display_scale": 0.01,
//       "decimals": 2
//     },
//     {
//       "symbol": "overscan_x",
//       "name": "Horizontal overscan",
//       "description": "Zoom or mirror horizontally; zero blanks the image",
//       "minimum": -125,
//       "maximum": 125,
//       "step": 1,
//       "initial": 100,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "overscan_y",
//       "name": "Vertical overscan",
//       "description": "Zoom or mirror vertically; zero blanks the image",
//       "minimum": -125,
//       "maximum": 125,
//       "step": 1,
//       "initial": 100,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "dot_mask",
//       "name": "Dot mask strength",
//       "description": "Alternate green and magenta display-pixel columns",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 10,
//       "initial": 30,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "sharpness",
//       "name": "Sharpness",
//       "description": "Horizontal reconstruction sharpness",
//       "minimum": 1,
//       "maximum": 3,
//       "step": 1,
//       "initial": 1,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "scanline_weight",
//       "name": "Scanline weight",
//       "description": "Width of the simulated scanline beam",
//       "minimum": 10,
//       "maximum": 50,
//       "step": 5,
//       "initial": 30,
//       "scale": 0.01,
//       "suffix": "",
//       "display_scale": 0.01,
//       "decimals": 2
//     },
//     {
//       "symbol": "luminance",
//       "name": "Luminance",
//       "description": "Increase the scanline beam brightness",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "interlace",
//       "name": "Interlacing",
//       "description": "Alternate fields for inputs with at least 400 lines",
//       "minimum": 0,
//       "maximum": 1,
//       "step": 1,
//       "initial": 1,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "saturation",
//       "name": "Saturation",
//       "description": "Adjust the final color saturation",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01,
//       "suffix": "%"
//     }
//   ]
// }
// cabinet-shader-end

// CRT-interlaced
// 
// Copyright (C) 2010-2012 cgwg, Themaister and DOLLS
// 
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
// 
// (cgwg gave their consent to have the original version of this shader
// distributed under the GPL in this message:
// 
// http://board.byuu.org/viewtopic.php?p=26075#p26075
// 
// "Feel free to distribute my shaders under the GPL. After all, the
// barrel distortion code was taken from the Curvature shader, which is
// under the GPL."
// )
// This shader variant is pre-configured with screen curvature

struct CrtGeometry
{
    float distance;
    float radius;
    vec2 aspect;
    vec2 sine;
    vec2 cosine;
};

float crt_nonzero(float value)
{
    return (value < 0.0 ? -1.0 : 1.0) * max(abs(value), 0.0001);
}

vec2 crt_backward(vec2 xy, CrtGeometry geometry)
{
    float distance = geometry.distance;
    float radius = geometry.radius;
    float a = dot(xy, xy) + distance * distance;
    float b = 2.0 * (radius * (dot(xy, geometry.sine) - distance * geometry.cosine.x * geometry.cosine.y)
                     - distance * distance);
    float c = distance * distance + 2.0 * radius * distance * geometry.cosine.x * geometry.cosine.y;
    float intersection = (-b - sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
    vec2 point = intersection * xy / radius + geometry.sine;
    vec2 tangent = geometry.sine / geometry.cosine;
    vec2 projected = point / geometry.cosine;
    a = dot(tangent, tangent) + 1.0;
    b = -2.0 * dot(projected, tangent);
    c = dot(projected, projected) - 1.0;
    float angle = clamp((-b + sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a), -1.0, 1.0);
    vec2 uv = (point - angle * geometry.sine) / geometry.cosine;
    float r = radius * acos(angle);
    // Use the analytic center limit instead of perturbing both tilt angles like the reference.
    float scale = r < 0.0001 ? radius : r / crt_nonzero(sin(r / radius));
    return uv * scale;
}

vec2 crt_forward(vec2 uv, CrtGeometry geometry)
{
    float r = max(length(uv), 0.0001);
    uv *= sin(r / geometry.radius) / r;
    float x = 1.0 - cos(r / geometry.radius);
    float denominator = geometry.distance / geometry.radius + x * geometry.cosine.x * geometry.cosine.y
                        + dot(uv, geometry.sine);
    return geometry.distance * (uv * geometry.cosine - x * geometry.sine) / crt_nonzero(denominator);
}

vec3 crt_stretch(CrtGeometry geometry)
{
    vec2 center = crt_backward(-geometry.radius * geometry.sine /
                              (1.0 + geometry.radius / geometry.distance * geometry.cosine.x * geometry.cosine.y), geometry);
    vec2 extent = 0.5 * geometry.aspect;
    vec2 low = vec2(crt_forward(vec2(-extent.x, center.y), geometry).x,
                    crt_forward(vec2(center.x, -extent.y), geometry).y) / geometry.aspect;
    vec2 high = vec2(crt_forward(vec2(extent.x, center.y), geometry).x,
                     crt_forward(vec2(center.x, extent.y), geometry).y) / geometry.aspect;
    return vec3((high + low) * geometry.aspect * 0.5, max(max(high.x - low.x, high.y - low.y), 0.0001));
}

float crt_corner(vec2 position, vec2 aspect, vec2 overscan, float size, float smoothness)
{
    position = (position - vec2(0.5)) * overscan + vec2(0.5);
    position = min(position, vec2(1.0) - position) * aspect;
    vec2 distance = vec2(size) - min(position, vec2(size));
    return clamp((size - length(distance)) * smoothness, 0.0, 1.0);
}

vec3 crt_sample(vec2 position, float gamma)
{
    vec2 pixel = frame_texel_size();
    float x = position.x / pixel.x - 0.5;
    float left = (floor(x) + 0.5) * pixel.x;
    // Reconstruct fractional horizontal taps consistently with nearest or linear native samplers.
    vec3 value = mix(sample_frame(vec2(left, position.y)), sample_frame(vec2(left + pixel.x, position.y)), fract(x));
    return pow(max(value, vec3(0.0)), vec3(gamma));
}

vec3 crt_beam(float distance, vec3 color, float weight, float luminance)
{
    vec3 width = 2.0 + 2.0 * pow(color, vec3(4.0));
    vec3 profile = vec3(distance / weight) * inversesqrt(0.5 * width);
    return (luminance + 1.4) * exp(-pow(profile, width)) / (0.6 + 0.2 * width);
}

vec3 crt_saturation(vec3 color, float saturation)
{
    vec3 weighting = vec3(0.3, 0.6, 0.1);
    if (length(color) * 0.5775 < 0.5)
    {
        weighting = 2.0 * weighting * weighting;
    }
    return mix(vec3(dot(color, weighting)), color, saturation);
}

vec3 crt_geom(vec3 color, vec2 position, float frame_count, float crt_gamma, float inverse_gamma,
              float monitor_gamma, float view_distance, float curvature, float radius,
              float corner_size, float corner_smoothness, float tilt_x, float tilt_y,
              float overscan_x, float overscan_y, float dot_mask, float sharpness,
              float scanline_weight, float luminance, float interlace, float saturation)
{
    vec2 overscan = vec2(overscan_x, overscan_y);
    if (abs(overscan.x) < 0.0001 || abs(overscan.y) < 0.0001)
    {
        return vec3(0.0);
    }
    vec2 aspect = vec2(1.0, output_size().y / output_size().x);
    vec2 xy = (position - vec2(0.5)) / overscan + vec2(0.5);
    if (curvature > 0.5)
    {
        CrtGeometry geometry;
        geometry.distance = view_distance;
        geometry.radius = radius;
        geometry.aspect = aspect;
        geometry.sine = sin(vec2(tilt_x, tilt_y));
        geometry.cosine = cos(vec2(tilt_x, tilt_y));
        vec3 stretch = crt_stretch(geometry);
        vec2 point = (position - vec2(0.5)) * aspect * stretch.z + stretch.xy;
        xy = crt_backward(point, geometry) / overscan / aspect + vec2(0.5);
    }
    float border = crt_corner(xy, aspect, overscan, corner_size, corner_smoothness);
    if (border <= 0.0 || any(lessThan(xy, vec2(0.0))) || any(greaterThan(xy, vec2(1.0))))
    {
        return vec3(0.0);
    }
    vec2 input_size = frame_size();
    float field_size = interlace > 0.5 ? clamp(floor(input_size.y / 200.0), 1.0, 2.0) : 1.0;
    vec2 field = vec2(0.0, field_size > 1.5 ? mod(frame_count, 2.0) : 0.0);
    vec2 interlace_factor = vec2(1.0, field_size);
    vec2 ratio = (xy * input_size - vec2(0.5) + field) / interlace_factor;
    vec2 fraction = fract(ratio);
    xy = (floor(ratio) * interlace_factor + vec2(0.5) - field) / input_size;
    vec2 one = interlace_factor / vec2(sharpness * input_size.x, input_size.y);

    const float pi = 3.141592653589;
    vec4 coefficients = max(abs(pi * vec4(1.0 + fraction.x, fraction.x, 1.0 - fraction.x, 2.0 - fraction.x)), vec4(0.0001));
    coefficients = 2.0 * sin(coefficients) * sin(coefficients / 2.0) / (coefficients * coefficients);
    coefficients /= dot(coefficients, vec4(1.0));
    vec3 row = clamp(crt_sample(xy - vec2(one.x, 0.0), crt_gamma) * coefficients.x
                     + crt_sample(xy, crt_gamma) * coefficients.y
                     + crt_sample(xy + vec2(one.x, 0.0), crt_gamma) * coefficients.z
                     + crt_sample(xy + vec2(2.0 * one.x, 0.0), crt_gamma) * coefficients.w, vec3(0.0), vec3(1.0));
    xy.y += one.y;
    vec3 next_row = clamp(crt_sample(xy - vec2(one.x, 0.0), crt_gamma) * coefficients.x
                          + crt_sample(xy, crt_gamma) * coefficients.y
                          + crt_sample(xy + vec2(one.x, 0.0), crt_gamma) * coefficients.z
                          + crt_sample(xy + vec2(2.0 * one.x, 0.0), crt_gamma) * coefficients.w, vec3(0.0), vec3(1.0));

    float filter_width = input_size.y / output_size().y / 3.0;
    vec3 weights = (crt_beam(fraction.y, row, scanline_weight, luminance)
                    + crt_beam(fraction.y + filter_width, row, scanline_weight, luminance)
                    + crt_beam(abs(fraction.y - filter_width), row, scanline_weight, luminance)) / 3.0;
    vec3 next_weights = (crt_beam(1.0 - fraction.y, next_row, scanline_weight, luminance)
                         + crt_beam(abs(1.0 - fraction.y - filter_width), next_row, scanline_weight, luminance)
                         + crt_beam(abs(1.0 - fraction.y + filter_width), next_row, scanline_weight, luminance)) / 3.0;
    vec3 result = (row * weights + next_row * next_weights) * border;
    vec3 mask = mix(vec3(1.0, 1.0 - dot_mask, 1.0), vec3(1.0 - dot_mask, 1.0, 1.0 - dot_mask),
                    mod(floor(position.x * output_size().x), 2.0));
    result *= mask;
    if (inverse_gamma > 0.5)
    {
        // Brightness compensation can exceed one; keep both square roots within their domains.
        result = clamp(result, vec3(0.0), vec3(1.0));
        vec3 circle = result - vec3(1.0);
        float power = 1.0 / ((-0.7 * (1.0 - scanline_weight) + 1.0) * (1.0 - 0.5 * dot_mask)) - 1.25;
        result = mix(sqrt(result), sqrt(max(vec3(1.0) - circle * circle, vec3(0.0))), power);
    }
    else
    {
        result = pow(max(result, vec3(0.0)), vec3(1.0 / monitor_gamma));
    }
    return clamp(crt_saturation(result, saturation), vec3(0.0), vec3(1.0));
}
