#version 450
// One triangle twice the size of the screen, so presenting a finished frame needs no vertex buffer
// at all. Clipping cuts it down to the viewport, which the presenter sets to the rectangle the
// picture goes in (vk/present_fit.h): that is what letterboxes it. The texture coordinates run
// from 0 to 1 across that rectangle and on to 2 at the corners clipping removes.
layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
