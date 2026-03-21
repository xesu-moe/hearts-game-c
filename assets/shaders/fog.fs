#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float time;
uniform float opacity;
uniform float aspect; /* card height / width, ~1.5 */

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

float warped_fbm(vec2 p, float t)
{
    vec2 q = vec2(fbm(p), fbm(p + vec2(5.2, 1.3)));
    vec2 r = vec2(fbm(p + 4.0 * q + vec2(1.7 + t * 0.15, 9.2 - t * 0.1)),
                  fbm(p + 4.0 * q + vec2(8.3 - t * 0.12, 2.8 + t * 0.08)));
    return fbm(p + 4.0 * r);
}

void main()
{
    vec2 uv = fragTexCoord;
    float t = time * 0.5;

    /* --- Aspect-corrected center ---
     * UV 0-1 maps to the card rect directly.
     * Correct Y so circles render as circles on screen. */
    vec2 centered = uv - 0.5;
    vec2 corrected = vec2(centered.x, centered.y / aspect);
    float rdist = length(corrected);
    float angle = atan(corrected.y, corrected.x);

    /* --- Spiral arms pulling into center --- */
    float spiral_wind = 3.0 + 1.5 / (rdist + 0.05);
    float swirled = angle + t * 1.5 + rdist * spiral_wind;

    /* 3 main spiral arms */
    float arms = sin(swirled * 3.0);
    float arm_bright = smoothstep(-0.2, 0.6, arms);
    float arm_dark = smoothstep(0.2, -0.6, arms);

    /* Finer secondary arms */
    float fine = sin(swirled * 6.0 + t * 0.8) * 0.5 + 0.5;
    fine = pow(fine, 0.8) * 0.3;

    /* --- Billowy cloud texture --- */
    vec2 warped = vec2(cos(swirled), sin(swirled)) * rdist;
    float billow = warped_fbm(warped * 2.0, t);
    float detail = fbm(warped * 5.0 + vec2(t * 0.2, -t * 0.1));

    /* --- Brightness/depth --- */
    float eye = smoothstep(0.06, 0.0, rdist);
    float eye_ring = smoothstep(0.04, 0.08, rdist) * smoothstep(0.15, 0.08, rdist);

    float brightness = arm_bright * 0.4
                     + billow * 0.3
                     + detail * 0.15
                     + fine
                     + eye * 0.5;
    float depth_dark = smoothstep(0.1, 0.4, rdist) * 0.4;
    brightness = brightness - depth_dark - arm_dark * 0.25;
    brightness = clamp(brightness, 0.0, 1.0);

    /* --- Grey color palette --- */
    vec3 very_dark  = vec3(0.08, 0.09, 0.12);
    vec3 dark       = vec3(0.22, 0.24, 0.30);
    vec3 mid        = vec3(0.45, 0.47, 0.53);
    vec3 bright     = vec3(0.72, 0.74, 0.80);
    vec3 eye_white  = vec3(0.90, 0.92, 0.96);

    vec3 fog_color;
    if (brightness < 0.25) {
        fog_color = mix(very_dark, dark, brightness * 4.0);
    } else if (brightness < 0.5) {
        fog_color = mix(dark, mid, (brightness - 0.25) * 4.0);
    } else if (brightness < 0.75) {
        fog_color = mix(mid, bright, (brightness - 0.5) * 4.0);
    } else {
        fog_color = mix(bright, eye_white, (brightness - 0.75) * 4.0);
    }

    fog_color = mix(fog_color, eye_white, eye * 0.9);
    fog_color *= (1.0 - eye_ring * 0.2);

    /* --- Soft edge fade at card borders --- */
    float edge = smoothstep(0.0, 0.05, uv.x) * smoothstep(1.0, 0.95, uv.x)
               * smoothstep(0.0, 0.05, uv.y) * smoothstep(1.0, 0.95, uv.y);

    float alpha = edge * opacity;
    finalColor = vec4(fog_color, alpha);
}
