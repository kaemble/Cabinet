// cabinet-shader-begin
// {
//   "id": "film_noise",
//   "name": "Film grain",
//   "entry": "film_noise",
//   "context": true,
//   "parameters": [
//     {
//       "symbol": "intensity",
//       "name": "Intensity",
//       "description": "How visible the grain is; zero preserves the input",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 1,
//       "initial": 50,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "variance",
//       "name": "Variance",
//       "description": "Variance of the Gaussian noise; lower values look smoother",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 1,
//       "initial": 40,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "mean",
//       "name": "Mean",
//       "description": "Brightness of the noise; 50% is neutral",
//       "minimum": 0,
//       "maximum": 100,
//       "step": 1,
//       "initial": 50,
//       "scale": 0.01,
//       "suffix": "%"
//     },
//     {
//       "symbol": "signal_to_noise_ratio",
//       "name": "Signal-to-noise ratio",
//       "description": "Reduce grain in brighter pixels; zero disables this feature",
//       "minimum": 0,
//       "maximum": 16,
//       "step": 1,
//       "initial": 6,
//       "scale": 1,
//       "suffix": ""
//     }
//   ]
// }
// cabinet-shader-end

// Ported from reference/shader/FilmGrain.fx.
vec3 film_noise(vec3 color, vec2 position, float frame_count,
                float intensity, float variance, float mean, float signal_to_noise_ratio)
{
    const float pi = 3.1415927;
    // Approximate ReShade's millisecond timer with Cabinet's frame counter at 60 Hz.
    float timer = frame_count * (1000.0 / 60.0) * 0.0022337;
    float seed = dot(position, vec2(12.9898, 78.233));
    float uniform_noise1 = fract(sin(seed) * 43758.5453 + timer);
    float uniform_noise2 = fract(cos(seed) * 53758.5453 - timer);

    float inverse_luma = 1.0 - dot(color, vec3(1.0 / 3.0));
    float attenuation = (signal_to_noise_ratio != 0.0) ? pow(abs(inverse_luma), signal_to_noise_ratio) : 1.0;
    float noise_variance = variance * variance * attenuation;

    uniform_noise1 = max(uniform_noise1, 0.0001);
    float radius = sqrt(-log(uniform_noise1));
    float theta = 2.0 * pi * uniform_noise2;
    float gaussian_noise = noise_variance * radius * cos(theta) + mean;
    float grain = mix(1.0 + intensity, 1.0 - intensity, gaussian_noise);
    return color * grain;
}
