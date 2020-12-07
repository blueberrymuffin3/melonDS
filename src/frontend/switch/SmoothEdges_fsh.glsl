#version 460

layout (location = 0) out vec4 outColor;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor; 

layout (binding = 0) uniform sampler2D inTexture;


void main()
{
    // adapted from:
    // https://csantosbh.wordpress.com/2014/02/05/automatically-detecting-the-texture-filter-threshold-for-pixelated-magnifications/

    // this would have been better in the vertex shader
    vec2 uvTexels = inUV * textureSize(inTexture, 0);

    vec2 alpha = 0.7 * vec2(dFdx(uvTexels.x), dFdy(uvTexels.y));
    vec2 x = fract(uvTexels);
    vec2 x_ = clamp(0.5 / alpha * x, 0.0, 0.5)
        + clamp(0.5 / alpha * (x - 1.0) + 0.5, 0.0, 0.5);

    vec2 texCoord = (floor(uvTexels) + x_) / textureSize(inTexture, 0);
    outColor = texture(inTexture, texCoord) * inColor;
}