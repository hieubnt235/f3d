struct DrawData {
    color: vec4f,
    scale: vec2f,
    offset: vec2f,
}
@group(0) @binding(0)
var<uniform> draw_data: DrawData;

@vertex
fn vs(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
    let pos = array(vec2f(0, 0.5), vec2f(-0.5, -0.5), vec2f(0.5, -0.5));
    return vec4f(pos[idx] * draw_data.scale + draw_data.offset, 0.0, 1.0);
}

@fragment
fn fs() -> @location(0) vec4f {
    return draw_data.color;
}
