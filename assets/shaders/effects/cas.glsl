// cabinet-shader-begin
// {
//   "id": "cas",
//   "name": "CAS sharpening",
//   "entry": "cas_sharpen",
//   "context": true,
//   "parameters": [
//     {
//       "symbol": "contrast",
//       "name": "Contrast adaptation",
//       "description": "Sharpen more strongly in high-contrast areas; zero still sharpens",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 5,
//       "initial": 0,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "sharpening",
//       "name": "Sharpening intensity",
//       "description": "Blend the sharpened result with the input; zero preserves the input",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01,
//       "suffix": "%"
//     }
//   ]
// }
// cabinet-shader-end

// LICENSE
// =======
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE

// Initial port to ReShade: SLSNe    https://gist.github.com/SLSNe/bbaf2d77db0b2a2a0755df581b3cf00c
// use a linear colorspace rather than sRGB, as recommended by the original AMD documentation from FidelityFX.

vec3 cas_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + vec3(0.055)) / 1.055, vec3(2.4)),
               step(vec3(0.04045), color));
}

vec3 cas_srgb(vec3 color)
{
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - vec3(0.055),
               step(vec3(0.0031308), color));
}

vec3 cas_sharpen(vec3 color, vec2 position, float frame_count, float contrast, float sharpening)
{
    if (sharpening == 0.0)
    {
        return color;
    }
    vec2 pixel = frame_texel_size();
    // 3x3 neighborhood: a b c / d e f / g h i.
    vec3 a = cas_linear(sample_frame(position + pixel * vec2(-1.0, -1.0)));
    vec3 b = cas_linear(sample_frame(position + pixel * vec2(0.0, -1.0)));
    vec3 c = cas_linear(sample_frame(position + pixel * vec2(1.0, -1.0)));
    vec3 d = cas_linear(sample_frame(position + pixel * vec2(-1.0, 0.0)));
    vec3 e = cas_linear(color);
    vec3 f = cas_linear(sample_frame(position + pixel * vec2(1.0, 0.0)));
    vec3 g = cas_linear(sample_frame(position + pixel * vec2(-1.0, 1.0)));
    vec3 h = cas_linear(sample_frame(position + pixel * vec2(0.0, 1.0)));
    vec3 i = cas_linear(sample_frame(position + pixel * vec2(1.0, 1.0)));

    vec3 minimum = min(min(min(d, e), min(f, b)), h);
    minimum += min(minimum, min(min(a, c), min(g, i)));
    vec3 maximum = max(max(max(d, e), max(f, b)), h);
    maximum += max(maximum, max(max(a, c), max(g, i)));

    // Guard black channels and use sqrt directly to avoid the reference's 0/0 and rsqrt(0).
    vec3 amplitude = clamp(min(minimum, vec3(2.0) - maximum) / max(maximum, vec3(0.0001)),
                           vec3(0.0), vec3(1.0));
    vec3 weight = -sqrt(amplitude) / (8.0 - 3.0 * contrast);
    vec3 sharpened = clamp(((b + d + f + h) * weight + e) / (vec3(1.0) + 4.0 * weight),
                           vec3(0.0), vec3(1.0));
    return cas_srgb(mix(e, sharpened, sharpening));
}
