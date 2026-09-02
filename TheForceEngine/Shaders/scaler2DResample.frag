// 2D-Scaler - pass 1: edge-aware resample ("TWODS0" in the original .fx file).
//
// Ported from HLSL to GLSL. Original shader:
//   2D-Scaler for ReShade
//   Copyright (C) 2019 guest(r) - guest.r@gmail.com
//   Licensed under the GNU General Public License v2 (or later).
//
// This reads the already window-scaled game image (produced by the normal
// TFE blit stage) and resamples it with an edge-aware, bilinear-like filter:
// for each output texel it blends the four surrounding "diagonal" samples,
// weighting each corner by how well it preserves local edges rather than
// blurring across them. This softens the blocky look of a nearest-neighbor
// upscale without turning it into a flat blur. Pass 2 (scaler2DDeblur.frag)
// sharpens the result back up.
uniform sampler2D SourceImage;
uniform vec2 PixelSize;     // 1 / window width, 1 / window height ("ReShade::PixelSize")
uniform float FilterWidth;  // 'Filter Width' UI parameter ("o" in the original shader)

in vec2 Frag_UV;
out vec4 Out_Color;

// Corresponds to the "texture2d()" helper in the original shader: reads the
// 4 diagonal neighbors around texcoord and blends them, weighting by which
// diagonal pair better preserves the local edge.
vec3 sampleDiagonal(vec2 texcoord, vec2 invSize)
{
    vec3 dt = vec3(1.0);

    vec3 s00 = texture(SourceImage, texcoord + vec2(-invSize.x, -invSize.y)).rgb;
    vec3 s20 = texture(SourceImage, texcoord + vec2( invSize.x, -invSize.y)).rgb;
    vec3 s22 = texture(SourceImage, texcoord + vec2( invSize.x,  invSize.y)).rgb;
    vec3 s02 = texture(SourceImage, texcoord + vec2(-invSize.x,  invSize.y)).rgb;

    float m1 = dot(abs(s00 - s22), dt) + 0.001;
    float m2 = dot(abs(s02 - s20), dt) + 0.001;

    return 0.5 * (m2 * (s00 + s22) + m1 * (s02 + s20)) / (m1 + m2);
}

void main()
{
    vec3 dt = vec3(1.0);

    // Calculating texel coordinates.
    vec2 invSize = FilterWidth * PixelSize;
    vec2 size = 1.0 / invSize;

    vec2 pos = Frag_UV * size;
    vec2 fp = fract(pos);
    vec2 dx = vec2(invSize.x, 0.0);
    vec2 dy = vec2(0.0, invSize.y);
    vec2 g1 = vec2(invSize.x, invSize.y);
    vec2 g2 = vec2(-invSize.x, invSize.y);

    vec2 pC4 = floor(pos) * invSize + 0.5 * invSize;

    // Reading the texels.
    vec3 C0 = sampleDiagonal(pC4 - g1, invSize);
    vec3 C1 = sampleDiagonal(pC4 - dy, invSize);
    vec3 C2 = sampleDiagonal(pC4 - g2, invSize);
    vec3 C3 = sampleDiagonal(pC4 - dx, invSize);
    vec3 C4 = sampleDiagonal(pC4,      invSize);
    vec3 C5 = sampleDiagonal(pC4 + dx, invSize);
    vec3 C6 = sampleDiagonal(pC4 + g2, invSize);
    vec3 C7 = sampleDiagonal(pC4 + dy, invSize);
    vec3 C8 = sampleDiagonal(pC4 + g1, invSize);

    vec3 ul, ur, dl, dr;
    float m1, m2;

    m1 = dot(abs(C0 - C4), dt) + 0.001;
    m2 = dot(abs(C1 - C3), dt) + 0.001;
    ul = (m2 * (C0 + C4) + m1 * (C1 + C3)) / (m1 + m2);

    m1 = dot(abs(C1 - C5), dt) + 0.001;
    m2 = dot(abs(C2 - C4), dt) + 0.001;
    ur = (m2 * (C1 + C5) + m1 * (C2 + C4)) / (m1 + m2);

    m1 = dot(abs(C3 - C7), dt) + 0.001;
    m2 = dot(abs(C6 - C4), dt) + 0.001;
    dl = (m2 * (C3 + C7) + m1 * (C6 + C4)) / (m1 + m2);

    m1 = dot(abs(C4 - C8), dt) + 0.001;
    m2 = dot(abs(C5 - C7), dt) + 0.001;
    dr = (m2 * (C4 + C8) + m1 * (C5 + C7)) / (m1 + m2);

    vec3 result = 0.5 * ((dr * fp.x + dl * (1.0 - fp.x)) * fp.y + (ur * fp.x + ul * (1.0 - fp.x)) * (1.0 - fp.y));

    Out_Color = vec4(result, 1.0);
}
