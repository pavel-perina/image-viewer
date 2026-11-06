#version 440
// First pass of Magic Kernel Sharp 2013: the tiles, in display encoding at one texel per pixel
// of the level, become linear light at the view's width. Later than this nothing is encoded,
// and the result can be negative, so it needs a float target.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float destSize;   // view width in device pixels
    float texSize;    // width of the source
    float ratio;      // source texels per device pixel
    float phase;      // the source position of the view's left edge
    float spacing;    // how far the kernel is stretched, in source texels
    float boundLow;   // the first texel of the image in the source
    float boundHigh;  // the texel after its last
};
layout(binding = 1) uniform sampler2D source;

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

vec3 toLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    const float centre = phase + qt_TexCoord0.x * destSize * ratio;
    const float radius = 2.5 * spacing;
    const int first = int(ceil(centre - 0.5 - radius));
    const int last = int(floor(centre - 0.5 + radius));
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = first; i <= last; ++i) {
        const float w = kernel((float(i) + 0.5 - centre) / spacing);
        // The edge pixel continues outwards, as nothing lies beyond the image.
        const float texel = clamp(float(i), boundLow, boundHigh - 1.0);
        sum += w * toLinear(texture(source, vec2((texel + 0.5) / texSize, qt_TexCoord0.y)).rgb);
        weights += w;
    }
    fragColor = vec4(sum / weights, 1.0);
}
