#version 450

// Uzywamy wylacznie location = 0 (inPosition), aby zachowac 100% zgodnosci z buforami
// wierzcholkow bez wektora inTangent (location = 3).
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightSpaceMvp;
} pcs;

void main() {
    vec4 clipPos = pcs.lightSpaceMvp * vec4(inPosition, 1.0);

    // Orthographic Near-Plane Pancaking wylacznie dla kaskad kierunkowych CSM (w == 1.0):
    // Chroni wysokie obiekty (np. wieze z kostek nad kaskada) przed wycieciem przez z < 0 w Vulkanie,
    // nie ingerujac w rzut perspektywiczny dla cubemapy swiatel punktowych (gdzie w != 1.0).
    if (abs(clipPos.w - 1.0) < 1e-4 && clipPos.z < 0.0) {
        clipPos.z = 0.0;
    }

    gl_Position = clipPos;
}
