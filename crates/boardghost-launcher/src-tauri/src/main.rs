// Prevent additional console window on Windows in release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use boardghost_launcher::state::AppState;
use boardghost_launcher::commands;

fn main() {
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
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
