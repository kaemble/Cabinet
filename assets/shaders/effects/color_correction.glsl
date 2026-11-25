// cabinet-shader-begin
// {
//   "id": "color_correction",
//   "name": "Color correction",
//   "entry": "correct_color",
//   "parameters": [
//     {
//       "symbol": "brightness",
//       "name": "Brightness",
//       "description": "Add or subtract brightness",
//       "minimum": -100,
//       "maximum": 100,
//       "step": 5,
//       "initial": 0,
//       "scale": 0.01
//     },
//     {
//       "symbol": "contrast",
//       "name": "Contrast",
//       "description": "Adjust contrast around middle gray",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     },
//     {
//       "symbol": "saturation",
//       "name": "Saturation",
//       "description": "Adjust color intensity; zero produces grayscale",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     },
//     {
//       "symbol": "gamma",
//       "name": "Gamma",
//       "description": "Adjust midtones; higher values brighten the image",
//       "minimum": 10,
//       "maximum": 300,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     },
//     {
//       "symbol": "red_gain",
//       "name": "Red gain",
//       "description": "Adjust the red channel",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     },
//     {
//       "symbol": "green_gain",
//       "name": "Green gain",
//       "description": "Adjust the green channel",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     },
//     {
//       "symbol": "blue_gain",
//       "name": "Blue gain",
//       "description": "Adjust the blue channel",
//       "minimum": 0,
//       "maximum": 200,
//       "step": 5,
//       "initial": 100,
//       "scale": 0.01
//     }
//   ]
// }
// cabinet-shader-end

vec3 correct_color(vec3 color, float brightness, float contrast, float saturation, float gamma,
                   float red_gain, float green_gain, float blue_gain)
{
    color = (color - vec3(0.5)) * contrast + vec3(0.5 + brightness);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, saturation) * vec3(red_gain, green_gain, blue_gain);
    return pow(clamp(color, 0.0, 1.0), vec3(1.0 / gamma));
}
