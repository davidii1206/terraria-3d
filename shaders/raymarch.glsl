#include <shared.inl>

DAXA_DECL_PUSH_CONSTANT(RaymarchPush, p)

layout(local_size_x = 8, local_size_y = 8) in;

void main() {
    uvec2 pixel_i = gl_GlobalInvocationID.xy;
    if (pixel_i.x >= p.frame_dim.x || pixel_i.y >= p.frame_dim.y)
        return;

    vec2 uv = vec2(pixel_i)/vec2(p.frame_dim);

    vec3 col = vec3(vec2(uv), 0.0);

    imageStore(daxa_image2D(p.image_id), ivec2(pixel_i.xy), vec4(col, 1));
}