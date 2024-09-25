#version 450

layout(location = 0) in vec4 position;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform BlankUbo
{
  vec4 colors[8];
} background;

layout(push_constant) uniform PushConstant
{
  uint viewportIndex;
} pushConstant;

void main()
{
  // outColor = background.colors[pushConstant.viewportIndex];
  vec2 uv = 0.5 * position.xy + 0.5;
  outColor = vec4(uv, 1, 1);
}
