#version 120

uniform sampler1D transforms;
uniform int numTransforms;

attribute vec2 position;
attribute vec2 texCoords;
attribute float spriteNumber;

varying vec2 texCoordsOut;

// Given a world position (for this lab, bottom-left is (0, 0), top-right of window is (1280, 720)),
// convert it to be in [-1, 1]^2, where y = -1 is the bottom and x = -1 is the left side.
vec2 cameraPos(vec2 worldPos) {
    return vec2(worldPos.x * 2.0 / 1280.0 - 1.0, worldPos.y * 2.0 / 720.0 - 1.0);
}

void main() {
    // Each sprite's transform is one texel, so sample at the texel's centre.
    vec4 transform = texture1D(transforms, (spriteNumber + 0.5) / float(numTransforms));
    vec2 worldPos = position * transform.xy + transform.zw;
    gl_Position = vec4(cameraPos(worldPos), 0.0, 1.0);
    texCoordsOut = texCoords;
}
