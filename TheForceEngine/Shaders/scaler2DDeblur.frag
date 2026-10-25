// 2D-Scaler - pass 2: edge-aware deblur/sharpen ("DEB" in the original .fx file).
//
// Ported from HLSL to GLSL. Original shader:
//   2D-Scaler for ReShade
//   Copyright (C) 2019 guest(r) - guest.r@gmail.com
//   Licensed under the GNU General Public License v2 (or later).
//
// Reads the output of pass 1 (scaler2DResample.frag) and sharpens it back up:
// a local 3x3 min/max envelope is computed, an edge-strength estimate decides
// how strongly to push the center pixel towards that envelope (the "Deblur"
// parameter scales this push), and a final edge-preserving weighted average
// (a bilateral-style filter based on similarity to the sharpened result)
// smooths out any leftover ringing.
uniform sampler2D SourceImage;
uniform vec2 PixelSize;     // 1 / window width, 1 / window height ("ReShade::PixelSize")
uniform float FilterWidth;  // 'Filter Width' UI parameter ("o" in the original shader)
uniform float Deblur;       // 'Deblur' UI parameter ("DBL" in the original shader)

in vec2 Frag_UV;
out vec4 Out_Color;

void main()
{
    // Calculating texel coordinates. The original shader always scales this
    // pass's texel size by an additional constant 1.375 relative to pass 1.
    vec2 invSize = 1.375 * FilterWidth * PixelSize;

    vec2 dx = vec2(invSize.x, 0.0);
    vec2 dy = vec2(0.0, invSize.y);
    vec2 g1 = vec2(invSize.x, invSize.y);
    vec2 g2 = vec2(-invSize.x, invSize.y);

    vec2 pC4 = Frag_UV;

    // Reading the texels.
    vec3 c00 = texture(SourceImage, pC4 - g1).rgb;
    vec3 c10 = texture(SourceImage, pC4 - dy).rgb;
    vec3 c20 = texture(SourceImage, pC4 - g2).rgb;
    vec3 c01 = texture(SourceImage, pC4 - dx).rgb;
    vec3 c11 = texture(SourceImage, pC4     ).rgb;
    vec3 c21 = texture(SourceImage, pC4 + dx).rgb;
    vec3 c02 = texture(SourceImage, pC4 + g2).rgb;
    vec3 c12 = texture(SourceImage, pC4 + dy).rgb;
    vec3 c22 = texture(SourceImage, pC4 + g1).rgb;

    vec3 d11 = c11;

    vec3 mn1 = min(min(c00, c01), c02);
    vec3 mn2 = min(min(c10, c11), c12);
    vec3 mn3 = min(min(c20, c21), c22);
    vec3 mx1 = max(max(c00, c01), c02);
    vec3 mx2 = max(max(c10, c11), c12);
    vec3 mx3 = max(max(c20, c21), c22);

    mn1 = min(min(mn1, mn2), mn3);
    mx1 = max(max(mx1, mx2), mx3);

    vec3 dif1 = abs(c11 - mn1) + 0.0001;
    vec3 dif2 = abs(c11 - mx1) + 0.0001;

    vec3 dt = vec3(1.0);
    float d1 = dot(abs(c00 - c22), dt) + 0.0001;
    float d2 = dot(abs(c20 - c02), dt) + 0.0001;
    float hl = dot(abs(c01 - c21), dt) + 0.0001;
    float vl = dot(abs(c10 - c12), dt) + 0.0001;

    float dif = pow(max(d1 + d2 + hl + vl - 0.2, 0.0) / (0.25 * dot(c01 + c10 + c12 + c21, dt) + 0.33), 0.75);

    dif = min(dif, 1.0);
    float db1 = max(mix(0.0, Deblur, dif), 1.0);

    dif1 = vec3(pow(dif1.x, db1), pow(dif1.y, db1), pow(dif1.z, db1));
    dif2 = vec3(pow(dif2.x, db1), pow(dif2.y, db1), pow(dif2.z, db1));

    d11 = vec3(
        (dif1.x * mx1.x + dif2.x * mn1.x) / (dif1.x + dif2.x),
        (dif1.y * mx1.y + dif2.y * mn1.y) / (dif1.y + dif2.y),
        (dif1.z * mx1.z + dif2.z * mn1.z) / (dif1.z + dif2.z));

    float k10 = 1.0 / (dot(abs(c10 - d11), dt) + 0.0001);
    float k01 = 1.0 / (dot(abs(c01 - d11), dt) + 0.0001);
    float k11 = 1.0 / (dot(abs(c11 - d11), dt) + 0.0001);
    float k21 = 1.0 / (dot(abs(c21 - d11), dt) + 0.0001);
    float k12 = 1.0 / (dot(abs(c12 - d11), dt) + 0.0001);
    float k00 = 1.0 / (dot(abs(c00 - d11), dt) + 0.0001);
    float k02 = 1.0 / (dot(abs(c02 - d11), dt) + 0.0001);
    float k20 = 1.0 / (dot(abs(c20 - d11), dt) + 0.0001);
    float k22 = 1.0 / (dot(abs(c22 - d11), dt) + 0.0001);

    float avg = 0.025 * (k10 + k01 + k11 + k21 + k12 + k00 + k02 + k20 + k22);

    k10 = max(k10 - avg, 0.0);
    k01 = max(k01 - avg, 0.0);
    k11 = max(k11 - avg, 0.0);
    k21 = max(k21 - avg, 0.0);
    k12 = max(k12 - avg, 0.0);
    k00 = max(k00 - avg, 0.0);
    k02 = max(k02 - avg, 0.0);
    k20 = max(k20 - avg, 0.0);
    k22 = max(k22 - avg, 0.0);

    c11 = (k10 * c10 + k01 * c01 + k11 * c11 + k21 * c21 + k12 * c12 + k00 * c00 + k02 * c02 + k20 * c20 + k22 * c22 + 0.0001 * c11)
        / (k10 + k01 + k11 + k21 + k12 + k00 + k02 + k20 + k22 + 0.0001);

    Out_Color = vec4(c11, 1.0);
}
