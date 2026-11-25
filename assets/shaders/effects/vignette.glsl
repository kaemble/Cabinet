// cabinet-shader-begin
// {
//   "id": "vignette",
//   "name": "Vignette",
//   "entry": "vignette",
//   "context": true,
//   "parameters": [
//     {
//       "symbol": "strength",
//       "name": "Strength",
//       "description": "Darken the edges; zero preserves the input",
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

// Centered vignette adapted from film_noise.glsl by hunterk.
// License: public domain.
vec3 vignette(vec3 color, vec2 position, float frame_count, float strength)
{
    float falloff = smoothstep(0.25, 0.75, length(position - vec2(0.5)));
    return color * (1.0 - falloff * strength);
}
