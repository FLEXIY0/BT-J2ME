//! Small GL helpers shared by the lite renderer and the X11 window blender.
//! Everything here works on OpenGL 2.1 (GLSL 1.20) as well as 3.3 core.

use anyhow::{Result, anyhow, bail};
use glow::HasContext;

/// OpenGL 3.3+ (core features such as VAOs, texture swizzle, timer queries).
pub fn modern(gl: &glow::Context) -> bool {
    let v = gl.version();
    !v.is_embedded && (v.major, v.minor) >= (3, 3)
}

/// Compile a program; attribute "pos" is bound to location 0.
pub fn program(gl: &glow::Context, vs: &str, fs: &str) -> Result<glow::Program> {
    unsafe {
        let program = gl.create_program().map_err(|e| anyhow!(e))?;
        let mut shaders = Vec::new();
        for (kind, src) in [(glow::VERTEX_SHADER, vs), (glow::FRAGMENT_SHADER, fs)] {
            let s = gl.create_shader(kind).map_err(|e| anyhow!(e))?;
            gl.shader_source(s, src);
            gl.compile_shader(s);
            if !gl.get_shader_compile_status(s) {
                bail!("shader compile: {}", gl.get_shader_info_log(s));
            }
            gl.attach_shader(program, s);
            shaders.push(s);
        }
        gl.bind_attrib_location(program, 0, "pos");
        gl.link_program(program);
        for s in shaders {
            gl.delete_shader(s);
        }
        if !gl.get_program_link_status(program) {
            bail!("shader link: {}", gl.get_program_info_log(program));
        }
        Ok(program)
    }
}

/// Unit quad (0,0)-(1,1) as a triangle strip in attribute 0.
pub struct Quad {
    vbo: glow::Buffer,
    vao: Option<glow::VertexArray>,
}

impl Quad {
    pub fn new(gl: &glow::Context) -> Result<Quad> {
        unsafe {
            let vbo = gl.create_buffer().map_err(|e| anyhow!(e))?;
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
            let v: [f32; 8] = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0];
            let bytes = std::slice::from_raw_parts(v.as_ptr() as *const u8, 32);
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, bytes, glow::STATIC_DRAW);
            gl.bind_buffer(glow::ARRAY_BUFFER, None);
            // Core profiles need a VAO; GL 2.1 has none.
            let vao = if modern(gl) {
                Some(gl.create_vertex_array().map_err(|e| anyhow!(e))?)
            } else {
                None
            };
            Ok(Quad { vbo, vao })
        }
    }

    pub fn draw(&self, gl: &glow::Context) {
        unsafe {
            gl.bind_vertex_array(self.vao);
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.vbo));
            gl.enable_vertex_attrib_array(0);
            gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, 8, 0);
            gl.draw_arrays(glow::TRIANGLE_STRIP, 0, 4);
            gl.disable_vertex_attrib_array(0);
            gl.bind_buffer(glow::ARRAY_BUFFER, None);
            gl.bind_vertex_array(None);
        }
    }

    pub fn destroy(&self, gl: &glow::Context) {
        unsafe {
            gl.delete_buffer(self.vbo);
            if let Some(v) = self.vao {
                gl.delete_vertex_array(v);
            }
        }
    }
}
