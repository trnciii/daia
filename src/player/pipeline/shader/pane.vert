#version 450

vec3 positions[] = vec3[](
    vec3(-1, 1, 0.1),
    vec3(-1, -1, 0.1),
    vec3(1, 1, 0.1),
    vec3(1, 1, 0.1),
    vec3(-1, -1, 0.1),
    vec3(1, -1, 0.1),

    vec3(0.0, -0.5, 0.5),
    vec3(0.5, 0.5, 0.5),
    vec3(-0.5, 0., 0.5)
);

layout(location = 0) out vec4 position;

void main() {
    position = vec4(positions[gl_VertexIndex], 1);
    gl_Position = position;
}
