// cabinet-shader-begin
// {
//   "id": "film_noise",
//   "name": "Film grain",
//   "entry": "film_noise",
//   "context": true,
//   "parameters": [
//     {
//       "symbol": "grain_str",
//       "name": "Grain strength",
//       "description": "Animated film grain intensity; zero disables grain",
//       "minimum": 0,
//       "maximum": 16,
//       "step": 1,
//       "initial": 8,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "hotspot",
//       "name": "Hotspot",
//       "description": "Brighten the center of the image",
//       "minimum": 0,
//       "maximum": 1,
//       "step": 1,
//       "initial": 0,
//       "scale": 1,
//       "suffix": ""
//     },
//     {
//       "symbol": "noise_toggle",
//       "name": "Film scratches",
//       "description": "Occasional procedural scratches and dust",
//       "minimum": 0,
//       "maximum": 1,
//       "step": 1,
//       "initial": 0,
//       "scale": 1,
//       "suffix": ""
//     }
//   ]
// }
// cabinet-shader-end

// Ported from film_noise.glsl by hunterk. License: public domain.
// replaces the noise1 LUT with procedural scratches and dust.
float film_grain(vec2 position, float strength, float timer)
{
    float x = (position.x + 4.0) * (position.y + 4.0) * ((mod(timer, 800.0) + 10.0) * 10.0);
    return (mod((mod(x, 13.0) + 1.0) * (mod(x, 123.0) + 1.0), 0.01) - 0.005) * strength;
}

float film_hash(float value)
{
    return fract(sin(value) * 43758.5453123);
}

vec3 film_noise(vec3 color, vec2 position, float frame_count,
                float grain_str, float hotspot, float noise_toggle)
{
    vec3 film = color + film_grain(position, grain_str, frame_count);

    float hotspot_mask = 1.0 - smoothstep(0.25, 0.75, length(position - vec2(0.5)));
    film += hotspot_mask * 0.2 * hotspot;

    float event = film_hash(frame_count);
    if (noise_toggle > 0.5 && (event > 0.99 || event < 0.01))
    {
        float phase = mod(frame_count, 92.0);
        float scratch_x = film_hash(phase + 17.0);
        float scratch = 1.0 - smoothstep(0.0, 0.002, abs(position.x - scratch_x));
        vec2 cell = floor(position * vec2(160.0, 120.0));
        float dust = step(0.997, film_hash(mod(cell.x + cell.y * 19.0 + phase, 1024.0)));
        float damage = max(scratch * 0.5, dust * 0.7);
        film = mix(film, vec3((event > 0.99) ? 1.0 : 0.0), damage);
    }
    return film;
}
