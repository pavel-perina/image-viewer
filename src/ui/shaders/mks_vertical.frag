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
        sum += w * texture(source, vec2(qt_TexCoord0.x, (texel + 0.5) / texSize)).rgb;
        weights += w;
    }
    fragColor = vec4(toDisplay(sum / weights), 1.0) * qt_Opacity;
}
