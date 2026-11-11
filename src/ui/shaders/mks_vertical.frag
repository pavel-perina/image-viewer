#version 440
// Second pass of Magic Kernel Sharp 2013: from linear light at the view's width to the view,
// back in display encoding.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float destSize;   // view height in device pixels
    float texSize;    // height of the source
    float ratio;      // source texels per device pixel
    float phase;      // the source position of the view's top edge
    float spacing;    // how far the kernel is stretched, in source texels
    float boundLow;   // the first texel of the image in the source
    float boundHigh;  // the texel after its last
    vec4 imageRect;   // left, top, right and bottom of the image in the view, from 0 to 1
    float ditherStep; // one step of the output
};
layout(binding = 1) uniform sampler2D source;

// Triangular noise from -1 to 1 per channel, from the pixel's place: two of Dave Hoskins' hashes
// without sine, which stay random at any position and need no integers. Added before the output
// is rounded, a step wide, it turns bands into an even grain finer than the eye resolves.
vec3 hashed(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

vec3 dither(vec2 p) {
    return hashed(p) + hashed(p + vec2(57.31, 19.17)) - 1.0;
}

float kernel(float x) {
    x = abs(x);
    if (x <= 0.5)
        return 17.0 / 16.0 - 1.75 * x * x;
    if (x <= 1.5)
        return (1.0 - x) * (1.75 - x);
    if (x <= 2.5)
        return -0.125 * (x - 2.5) * (x - 2.5);
    return 0.0;
}

vec3 toDisplay(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    if (qt_TexCoord0.x < imageRect.x || qt_TexCoord0.y < imageRect.y || qt_TexCoord0.x >= imageRect.z || qt_TexCoord0.y >= imageRect.w)
        discard;
    const float centre = phase + qt_TexCoord0.y * destSize * ratio;
    const float radius = 2.5 * spacing;
    const int first = int(ceil(centre - 0.5 - radius));
    const int last = int(floor(centre - 0.5 + radius));
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = first; i <= last; ++i) {
        const float w = kernel((float(i) + 0.5 - centre) / spacing);
        const float texel = clamp(float(i), boundLow, boundHigh - 1.0);
        // With an explicit level, as Direct3D cannot compile texture() in a loop of this length.
        sum += w * textureLod(source, vec2(qt_TexCoord0.x, (texel + 0.5) / texSize), 0.0).rgb;
        weights += w;
    }
    fragColor = vec4(toDisplay(sum / weights) + dither(gl_FragCoord.xy) * ditherStep, 1.0) * qt_Opacity;
}
