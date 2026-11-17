#version 120

varying vec2 texCoordsOut;

// Given a world position (for this lab, bottom-left is (0, 0), top-right of window is (1280, 720)),
// convert it to be in [-1, 1]^2, where y = -1 is the bottom and x = -1 is the left side.
vec2 cameraPos(vec2 worldPos) {
    return vec2(worldPos.x * 2.0 / 1280.0 - 1.0, worldPos.y * 2.0 / 720.0 - 1.0);
}

void main() {
    // Using window.draw, SFML supplies the already-transformed vertex position in gl_Vertex and the
    // texture coordinates in gl_MultiTexCoord0.
    gl_Position = vec4(cameraPos(gl_Vertex.xy), 0.0, 1.0);
    texCoordsOut = gl_MultiTexCoord0.xy;
}
