// rt_screen.glsl - screen-space helpers shared by the RT indirect compute passes.
// Requires rt_common.glsl (for `ubo`) to be included first.
#ifndef RT_SCREEN_GLSL
#define RT_SCREEN_GLSL

// Must match glm::perspective(..., 0.1f, 1000.0f) in VulkanRenderer::render().
const float RT_NEAR_Z = 0.1;
const float RT_FAR_Z  = 1000.0;

// Vulkan [0,1] depth -> positive view-space distance along the camera axis.
float rtLinearDepth(float d) {
    return (RT_NEAR_Z * RT_FAR_Z) / (RT_FAR_Z - d * (RT_FAR_Z - RT_NEAR_Z));
}

// Pixel centre (in pixels) + hardware depth -> world position.
// viewProj already contains the Vulkan Y flip, so NDC.y maps straight to the row.
vec3 rtWorldPos(vec2 pixelCenter, float d, vec2 size) {
    vec2 ndc = pixelCenter / size * 2.0 - 1.0;
    vec4 h = ubo.invViewProj * vec4(ndc, d, 1.0);
    return h.xyz / h.w;
}

// View-space position of a pixel from its linear depth (no hardware depth needed).
vec3 rtViewPosFromLinear(vec2 pixelCenter, float linZ, vec2 size) {
    vec2 ndc = pixelCenter / size * 2.0 - 1.0;
    vec4 h = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 dirV = mat3(ubo.view) * (h.xyz / h.w - ubo.cameraPos.xyz);
    return dirV * (linZ / max(-dirV.z, 1e-6));
}

// Octahedral normal encoding (unit vector <-> [-1,1]^2).
vec2 rtOctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0) {
        vec2 oldXY = e;
        vec2 signNotZero = vec2(oldXY.x >= 0.0 ? 1.0 : -1.0, oldXY.y >= 0.0 ? 1.0 : -1.0);
        e = (1.0 - abs(oldXY.yx)) * signNotZero;
    }
    return e;
}

vec3 rtOctDecode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 oldXY = n.xy;
        vec2 signNotZero = vec2(oldXY.x >= 0.0 ? 1.0 : -1.0, oldXY.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(oldXY.yx)) * signNotZero;
    }
    return normalize(n);
}

#endif
