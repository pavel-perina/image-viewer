#version 440
// A colour edit over the image as shown, the same stages as applyColor in core/color_edit.cpp:
// white balance by a matrix on linear RGB, a tone curve on the samples, lightness and chroma in Oklab, then a cube, from one lookup image
// made by colorLutImage: the cube's slices side by side, then a row per curve.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float cubeSize;      // 0 without a cube
    float chroma;        // gain on Oklab a and b
    float useTone;       // 1 to apply the tone curve
    float useLightness;  // 1 to apply the lightness curve
    float gamutMap;      // 1 to fit colours by chroma rather than clipping them
    vec4 domainMin;      // of the cube, R, G, B
    vec4 domainMax;
    float adjust;        // 0 passes the colours through, to be dithered
    float ditherStep;    // one step of the output, or 0 when another pass follows
    mat4 whiteMatrix;    // white balance on linear RGB, in its upper 3 x 3
    vec4 luminance;      // weights of R, G, B for the luminance of the picture's RGB
    float useWhite;      // 1 to apply white balance
    float cubeStrength;  // the cube's result mixed with its input, from 0 to 1
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D lut;

const int kCurveSamples = 256;

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

vec3 decode(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 encode(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

vec3 toOklab(vec3 c) {
    vec3 lms = vec3(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
                    0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
                    0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    lms = sign(lms) * pow(abs(lms), vec3(1.0 / 3.0));
    return vec3(0.2104542553 * lms.x + 0.7936177850 * lms.y - 0.0040720468 * lms.z,
                1.9779984951 * lms.x - 2.4285922050 * lms.y + 0.4505937099 * lms.z,
                0.0259040371 * lms.x + 0.7827717662 * lms.y - 0.8086757660 * lms.z);
}

vec3 fromOklab(vec3 lab) {
    vec3 lms = vec3(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
                    lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
                    lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    lms = lms * lms * lms;
    return vec3(4.0767416621 * lms.x - 3.3077115913 * lms.y + 0.2309699292 * lms.z,
                -1.2684380046 * lms.x + 2.6097574011 * lms.y - 0.3413193965 * lms.z,
                -0.0041960863 * lms.x - 0.7034186147 * lms.y + 1.7076147010 * lms.z);
}

bool inGamut(vec3 c) {
    return all(greaterThanEqual(c, vec3(-1e-6))) && all(lessThanEqual(c, vec3(1.0 + 1e-6)));
}

// As intoGamut in core/color_edit.cpp: lightness clamped, then as much chroma as fits, by halving.
vec3 intoGamut(vec3 lab) {
    lab.x = clamp(lab.x, 0.0, 1.0);
    vec3 rgb = fromOklab(lab);
    if (inGamut(rgb)) {
        return rgb;
    }
    float low = 0.0;
    float high = 1.0;
    for (int i = 0; i < 12; ++i) {
        const float middle = (low + high) * 0.5;
        if (inGamut(fromOklab(vec3(lab.x, lab.yz * middle)))) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return fromOklab(vec3(lab.x, lab.yz * low));
}

// As fitByChroma in core/white_balance.cpp: towards the gray of the same luminance until the
// colour fits.
vec3 fitByChroma(vec3 c) {
    const float gray = clamp(dot(c, luminance.rgb), 0.0, 1.0);
    float keep = 1.0;
    for (int i = 0; i < 3; ++i) {
        if (c[i] > 1.0) {
            keep = min(keep, (1.0 - gray) / (c[i] - gray));
        } else if (c[i] < 0.0) {
            keep = min(keep, gray / (gray - c[i]));
        }
    }
    return gray + (c - gray) * max(keep, 0.0);
}

float curveAt(int row, float x) {
    const float position = clamp(x, 0.0, 1.0) * float(kCurveSamples - 1);
    const int i = min(int(position), kCurveSamples - 2);
    return mix(texelFetch(lut, ivec2(i, row), 0).r, texelFetch(lut, ivec2(i + 1, row), 0).r, position - float(i));
}

vec3 cubeAt(ivec3 p, int n) {
    return texelFetch(lut, ivec2(p.r + p.b * n, p.g), 0).rgb;
}

// Tetrahedral, with the cases in the order ColorCube uses.
vec3 throughCube(vec3 c, int n) {
    vec3 f = clamp((c - domainMin.rgb) / (domainMax.rgb - domainMin.rgb), 0.0, 1.0) * float(n - 1);
    const ivec3 i = min(ivec3(f), ivec3(n - 2));
    f -= vec3(i);
    const vec3 c000 = cubeAt(i, n);
    const vec3 c111 = cubeAt(i + ivec3(1, 1, 1), n);
    vec3 first;
    vec3 second;
    vec3 w;
    if (f.r >= f.g) {
        if (f.g >= f.b) {
            first = cubeAt(i + ivec3(1, 0, 0), n); second = cubeAt(i + ivec3(1, 1, 0), n); w = f.rgb;
        } else if (f.r >= f.b) {
            first = cubeAt(i + ivec3(1, 0, 0), n); second = cubeAt(i + ivec3(1, 0, 1), n); w = f.rbg;
        } else {
            first = cubeAt(i + ivec3(0, 0, 1), n); second = cubeAt(i + ivec3(1, 0, 1), n); w = f.brg;
        }
    } else {
        if (f.b >= f.g) {
            first = cubeAt(i + ivec3(0, 0, 1), n); second = cubeAt(i + ivec3(0, 1, 1), n); w = f.bgr;
        } else if (f.b >= f.r) {
            first = cubeAt(i + ivec3(0, 1, 0), n); second = cubeAt(i + ivec3(0, 1, 1), n); w = f.gbr;
        } else {
            first = cubeAt(i + ivec3(0, 1, 0), n); second = cubeAt(i + ivec3(1, 1, 0), n); w = f.grb;
        }
    }
    return c000 + w.x * (first - c000) + w.y * (second - first) + w.z * (c111 - second);
}

void main() {
    const vec4 sampled = texture(source, qt_TexCoord0);
    if (sampled.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 c = sampled.rgb / sampled.a;
    const int n = adjust > 0.5 ? int(cubeSize + 0.5) : 0;
    if (adjust > 0.5 && useWhite > 0.5) {
        vec3 linear = mat3(whiteMatrix) * decode(clamp(c, 0.0, 1.0));
        if (gamutMap > 0.5) {
            linear = fitByChroma(linear);
        }
        c = encode(clamp(linear, 0.0, 1.0));
    }
    if (adjust > 0.5 && useTone > 0.5) {
        c = vec3(curveAt(n, c.r), curveAt(n, c.g), curveAt(n, c.b));
    }
    if (adjust > 0.5 && (useLightness > 0.5 || chroma != 1.0)) {
        vec3 lab = toOklab(decode(clamp(c, 0.0, 1.0)));
        if (useLightness > 0.5) {
            lab.x = curveAt(n + 1, lab.x);
        }
        lab.yz *= chroma;
        c = encode(clamp(gamutMap > 0.5 ? intoGamut(lab) : fromOklab(lab), 0.0, 1.0));
    }
    if (n >= 2) {
        c = mix(c, throughCube(c, n), cubeStrength);
    }
    c = clamp(c, 0.0, 1.0);
    if (ditherStep > 0.0) {
        c += dither(gl_FragCoord.xy) * ditherStep;
    }
    fragColor = vec4(c * sampled.a, sampled.a) * qt_Opacity;
}
