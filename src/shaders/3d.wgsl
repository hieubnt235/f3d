struct Vertex {
    @location(0) pos_px: vec2f,
}

struct VSOut {
    @builtin(position) position: vec4f,
}

struct DrawData {
    color: vec4f,
    resolution: vec2f,
    trans_px: vec2f,
    rot_cos_sin: vec2f,
    scale: vec2f,
}

@group(0) @binding(0)
var<uniform> uni: DrawData;

@vertex
fn vs(vtx: Vertex) -> VSOut {
    var out: VSOut;

    // transform
    let cos = uni.rot_cos_sin.x;
    let sin = uni.rot_cos_sin.y;

    let scaled_pos_px = vtx.pos_px * uni.scale;
    let rot_pos_px = vec2f(
            scaled_pos_px.x * cos - scaled_pos_px.y * sin,
            scaled_pos_px.x * sin + scaled_pos_px.y * cos,
        );
    let pos_px = rot_pos_px + uni.trans_px;

    // Convert pixel space to clip space, align 0->1 to -1->1, flip y down->up.
    let pos_clip_space: vec2f = ((pos_px * 2 / uni.resolution) - 1) * vec2f(1, -1);
    out.position = vec4f(pos_clip_space, 0.0, 1.0);
    return out;
}

@fragment
fn fs() -> @location(0) vec4f {
    return uni.color;
}
