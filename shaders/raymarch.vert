#version 460 core
layout (location = 0) in vec2 aPos; // Input: 2D vertex position


out vec2 fragCoordScreen;

void main()
{
    fragCoordScreen = aPos; // Pass position directly (-1 to 1)
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); // Output to screen space
}
