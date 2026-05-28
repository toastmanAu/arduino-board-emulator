// Prevent additional console window on Windows in release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use boardghost_launcher::state::AppState;
use boardghost_launcher::commands;

fn main() {
    // In dev builds, auto-populate BOARDGHOST_BOARDS / BOARDGHOST_RUNTIME so
    // that spawned `boardghost` children can locate the runtime tree even
    // when their CWD is deep under the workspace. CARGO_MANIFEST_DIR is
    // src-tauri/, three levels below the repo root.
    let manifest = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    if let Some(repo_root) = manifest.parent().and_then(|p| p.parent()).and_then(|p| p.parent()) {
        let runtime = repo_root.join("runtime");
        if runtime.is_dir() {
            if std::env::var_os("BOARDGHOST_BOARDS").is_none() {
                std::env::set_var("BOARDGHOST_BOARDS", runtime.join("boards"));
            }
            if std::env::var_os("BOARDGHOST_RUNTIME").is_none() {
                std::env::set_var("BOARDGHOST_RUNTIME", &runtime);
            }
        }
    }

    let projects_path = dirs::config_dir()
        .unwrap_or_else(std::env::temp_dir)
        .join("boardghost/projects.json");

    let state = AppState::new(projects_path).expect("init AppState");

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            commands::list_recent_projects,
            commands::add_project,
            commands::list_boards,
            commands::build_and_run,
            commands::stop,
            commands::screenshot,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
