// cabinet-shader-begin
// {
//   "id": "chromatic_aberration",
//   "name": "Chromatic aberration",
//   "entry": "chromatic_aberration",
//   "context": true,
//   "parameters": [
//     {
//       "symbol": "x_off_r",
//       "name": "Red X offset",
//       "description": "Shift the red channel horizontally; each step is 0.01% of frame width",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     },
//     {
//       "symbol": "y_off_r",
//       "name": "Red Y offset",
//       "description": "Shift the red channel vertically; each step is 0.01% of frame height",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     },
//     {
//       "symbol": "x_off_g",
//       "name": "Green X offset",
//       "description": "Shift the green channel horizontally; each step is 0.01% of frame width",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     },
//     {
//       "symbol": "y_off_g",
//       "name": "Green Y offset",
//       "description": "Shift the green channel vertically; each step is 0.01% of frame height",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     },
//     {
//       "symbol": "x_off_b",
//       "name": "Blue X offset",
//       "description": "Shift the blue channel horizontally; each step is 0.01% of frame width",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     },
//     {
//       "symbol": "y_off_b",
//       "name": "Blue Y offset",
//       "description": "Shift the blue channel vertically; each step is 0.01% of frame height",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 1,
//       "initial": 0,
//       "scale": 0.0001,
//       "suffix": ""
//     }
//   ]
// }
// cabinet-shader-end

// RGB channel-offset sampling adapted from film_noise.glsl by hunterk.
// License: public domain.
vec3 chromatic_aberration(vec3 color, vec2 position, float frame_count,
                          float x_off_r, float y_off_r, float x_off_g, float y_off_g,
                          float x_off_b, float y_off_b)
{
    vec3 red_light = sample_frame(position + vec2(x_off_r, y_off_r));
    vec3 green_light = sample_frame(position + vec2(x_off_g, y_off_g));
    vec3 blue_light = sample_frame(position + vec2(x_off_b, y_off_b));
    return vec3(red_light.r, green_light.g, blue_light.b);
}
