use anyhow::{anyhow, Result};
use std::path::PathBuf;
use tera::{Context, Tera};

use crate::BoardProfile;

const TEMPLATE: &str = include_str!("../templates/CMakeLists.txt.tera");

pub struct CodegenInput<'a> {
    pub sketch_cpp:  PathBuf,
    pub board:       &'a BoardProfile,
    pub runtime_dir: PathBuf,
    pub release:     bool,
    pub out_dir:     PathBuf,
}

pub fn generate_cmake(input: &CodegenInput) -> Result<PathBuf> {
    let mut tera = Tera::default();
    tera.add_raw_template("cml", TEMPLATE)?;

    let mut ctx = Context::new();
    ctx.insert("sketch_cpp",     &input.sketch_cpp.to_string_lossy());
    ctx.insert("runtime_dir",    &input.runtime_dir.to_string_lossy());
    ctx.insert("board_name",     &input.board.name);
    ctx.insert("display_width",  &input.board.display.width);
    ctx.insert("display_height", &input.board.display.height);
    ctx.insert("release",        &input.release);

    let rendered = tera.render("cml", &ctx)?;
    let out = input.out_dir.join("CMakeLists.txt");
    std::fs::write(&out, rendered)
        .map_err(|e| anyhow!("write {:?}: {e}", out))?;
    Ok(out)
}
