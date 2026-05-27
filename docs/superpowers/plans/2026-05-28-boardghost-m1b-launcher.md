# BoardGhost M1.B — Tauri Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Tauri v2 + Svelte desktop launcher on top of the M1.A `boardghost` CLI — project list, board selector, "Build & Run" button, build-output panel, serial-monitor panel — completing the M1 acceptance criteria in §10 of the spec.

**Architecture:** Tauri v2 desktop app with a Svelte+TypeScript frontend (left column: project list; right column: build/run controls + two scrollable log panels). The Tauri Rust backend persists `~/.config/boardghost/projects.json`, spawns `boardghost run` via `tokio::process::Command`, demuxes child stdout/stderr to two streams, and forwards lines to the webview as Tauri events (`build-log`, `serial-log`). No CLI library coupling — the launcher invokes the `boardghost` binary already on PATH (or via configured location).

**Tech Stack:** Tauri v2 (Rust), Svelte 5 + TypeScript, Vite, `tokio` for async process management, `serde` for JSON state. Existing M1.A engine + CLI are the substrate.

**Source spec:** [`docs/superpowers/specs/2026-05-27-arduino-board-emulator-design.md`](../specs/2026-05-27-arduino-board-emulator-design.md) §6 (Launcher).

**Predecessor plan:** [`docs/superpowers/plans/2026-05-27-boardghost-m1a-engine.md`](2026-05-27-boardghost-m1a-engine.md) shipped under tag `m1a-engine`.

---

## File structure produced by this plan

```
arduino-board-emulator/
├── crates/
│   └── boardghost-launcher/
│       ├── Cargo.toml                Workspace member (root crate only; frontend at sibling paths)
│       ├── package.json              Svelte + Vite + tauri JS API
│       ├── vite.config.ts
│       ├── svelte.config.js
│       ├── tsconfig.json
│       ├── index.html                Vite entry
│       ├── src/                      Frontend (Svelte + TS)
│       │   ├── main.ts
│       │   ├── App.svelte            Root layout (two columns)
│       │   ├── lib/
│       │   │   ├── api.ts            Typed wrappers around invoke() + event listeners
│       │   │   ├── ProjectList.svelte
│       │   │   ├── ProjectDetail.svelte
│       │   │   ├── BoardSelect.svelte
│       │   │   └── LogPanel.svelte
│       │   └── styles/
│       │       └── global.css
│       └── src-tauri/                Tauri Rust backend (separate crate)
│           ├── Cargo.toml
│           ├── tauri.conf.json
│           ├── build.rs
│           ├── icons/
│           │   ├── 32x32.png
│           │   ├── 128x128.png
│           │   └── icon.png
│           └── src/
│               ├── main.rs           tauri::Builder entrypoint, command registration
│               ├── commands.rs       #[tauri::command] handlers
│               ├── state.rs          AppState (running child, app handle)
│               ├── projects.rs       projects.json load/save
│               └── runner.rs         Spawn `boardghost` + stream stdout/stderr
├── Cargo.toml                        Workspace root — add boardghost-launcher members
├── README.md                         Add launcher section
└── .github/workflows/ci.yml          Add launcher build job
```

## Task list

The 15 tasks below. Each commits independently. Read the relevant spec section before starting a task.

---

## Task 1: Scaffold Tauri v2 + Svelte project skeleton

**Files:**
- Create: `crates/boardghost-launcher/package.json`
- Create: `crates/boardghost-launcher/vite.config.ts`
- Create: `crates/boardghost-launcher/svelte.config.js`
- Create: `crates/boardghost-launcher/tsconfig.json`
- Create: `crates/boardghost-launcher/tsconfig.node.json`
- Create: `crates/boardghost-launcher/index.html`
- Create: `crates/boardghost-launcher/src/main.ts`
- Create: `crates/boardghost-launcher/src/App.svelte`
- Create: `crates/boardghost-launcher/src/vite-env.d.ts`
- Create: `crates/boardghost-launcher/src/styles/global.css`
- Create: `crates/boardghost-launcher/.gitignore`

- [ ] **Step 1: Verify Node.js + npm are installed**

```bash
node --version  # expect >= 20
npm --version
```

If Node is missing, STOP and report BLOCKED with a request to install Node (e.g., via nvm). Tauri v2 needs Node for the frontend build chain.

- [ ] **Step 2: Create directory + package.json**

```bash
mkdir -p crates/boardghost-launcher/src/lib crates/boardghost-launcher/src/styles
```

`crates/boardghost-launcher/package.json`:

```json
{
  "name": "boardghost-launcher",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview",
    "check": "svelte-check --tsconfig ./tsconfig.json",
    "tauri": "tauri"
  },
  "dependencies": {
    "@tauri-apps/api": "^2.0.0"
  },
  "devDependencies": {
    "@sveltejs/vite-plugin-svelte": "^4.0.0",
    "@tauri-apps/cli": "^2.0.0",
    "svelte": "^5.0.0",
    "svelte-check": "^4.0.0",
    "tslib": "^2.6.0",
    "typescript": "^5.5.0",
    "vite": "^5.4.0"
  }
}
```

- [ ] **Step 3: Vite + Svelte configs**

`crates/boardghost-launcher/vite.config.ts`:

```typescript
import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

// Tauri v2 expects the frontend to be available at a fixed port during dev.
// See https://tauri.app/start/frontend/sveltekit/ for the canonical setup.
const host = process.env.TAURI_DEV_HOST;

export default defineConfig(async () => ({
  plugins: [svelte()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host ? { protocol: "ws", host, port: 1421 } : undefined,
    watch: { ignored: ["**/src-tauri/**"] },
  },
}));
```

`crates/boardghost-launcher/svelte.config.js`:

```javascript
import { vitePreprocess } from "@sveltejs/vite-plugin-svelte";

export default {
  preprocess: vitePreprocess(),
};
```

`crates/boardghost-launcher/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "module": "ESNext",
    "resolveJsonModule": true,
    "allowSyntheticDefaultImports": true,
    "esModuleInterop": true,
    "moduleResolution": "Bundler",
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true,
    "skipLibCheck": true,
    "isolatedModules": true,
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "types": ["svelte"]
  },
  "include": ["src/**/*.ts", "src/**/*.d.ts", "src/**/*.svelte"],
  "references": [{ "path": "./tsconfig.node.json" }]
}
```

`crates/boardghost-launcher/tsconfig.node.json`:

```json
{
  "compilerOptions": {
    "composite": true,
    "skipLibCheck": true,
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "allowSyntheticDefaultImports": true
  },
  "include": ["vite.config.ts"]
}
```

- [ ] **Step 4: Entry HTML + main.ts + root App**

`crates/boardghost-launcher/index.html`:

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>BoardGhost</title>
  </head>
  <body>
    <div id="app"></div>
    <script type="module" src="/src/main.ts"></script>
  </body>
</html>
```

`crates/boardghost-launcher/src/main.ts`:

```typescript
import "./styles/global.css";
import App from "./App.svelte";
import { mount } from "svelte";

const app = mount(App, { target: document.getElementById("app")! });
export default app;
```

`crates/boardghost-launcher/src/App.svelte`:

```svelte
<script lang="ts">
  // Real implementation lands in Task 8.
</script>

<main>
  <h1>BoardGhost</h1>
  <p>Launcher scaffolding — wiring lands in later tasks.</p>
</main>

<style>
  main { padding: 1rem; font-family: system-ui, sans-serif; }
</style>
```

`crates/boardghost-launcher/src/vite-env.d.ts`:

```typescript
/// <reference types="svelte" />
/// <reference types="vite/client" />
```

`crates/boardghost-launcher/src/styles/global.css`:

```css
:root {
  color-scheme: light dark;
  font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
  font-size: 14px;
  line-height: 1.4;
}

* { box-sizing: border-box; }

body, html {
  margin: 0;
  padding: 0;
  height: 100vh;
  width: 100vw;
  overflow: hidden;
}
```

- [ ] **Step 5: Launcher-specific .gitignore**

`crates/boardghost-launcher/.gitignore`:

```gitignore
node_modules/
dist/
.svelte-kit/
src-tauri/target/
src-tauri/gen/schemas/
```

- [ ] **Step 6: Install npm deps + verify build**

```bash
cd crates/boardghost-launcher
npm install
npm run check     # type-check Svelte + TS
npm run build     # produces dist/
ls dist/index.html
cd ../..
```

Expected: `dist/index.html` exists. No type errors.

- [ ] **Step 7: Commit**

```bash
git add crates/boardghost-launcher/package.json \
        crates/boardghost-launcher/vite.config.ts \
        crates/boardghost-launcher/svelte.config.js \
        crates/boardghost-launcher/tsconfig.json \
        crates/boardghost-launcher/tsconfig.node.json \
        crates/boardghost-launcher/index.html \
        crates/boardghost-launcher/src/ \
        crates/boardghost-launcher/.gitignore
git commit -m "feat(launcher): scaffold Svelte + Vite + TypeScript frontend"
```

---

## Task 2: Add Tauri v2 backend (src-tauri)

**Files:**
- Create: `crates/boardghost-launcher/src-tauri/Cargo.toml`
- Create: `crates/boardghost-launcher/src-tauri/tauri.conf.json`
- Create: `crates/boardghost-launcher/src-tauri/build.rs`
- Create: `crates/boardghost-launcher/src-tauri/src/main.rs`
- Create: `crates/boardghost-launcher/src-tauri/icons/icon.png` (placeholder)
- Create: `crates/boardghost-launcher/src-tauri/icons/32x32.png` (placeholder)
- Create: `crates/boardghost-launcher/src-tauri/icons/128x128.png` (placeholder)
- Modify: `Cargo.toml` (workspace root) — add `crates/boardghost-launcher/src-tauri` to members

- [ ] **Step 1: Add src-tauri to the workspace**

Edit the workspace `Cargo.toml` at the repo root. Update `members`:

```toml
[workspace]
resolver = "2"
members = [
    "crates/boardghost-cli",
    "crates/boardghost-launcher/src-tauri",
]
```

- [ ] **Step 2: Write `crates/boardghost-launcher/src-tauri/Cargo.toml`**

```toml
[package]
name = "boardghost-launcher"
version.workspace = true
edition.workspace = true
license.workspace = true
description = "Desktop launcher for the BoardGhost simulator"

[[bin]]
name = "boardghost-launcher"
path = "src/main.rs"

[build-dependencies]
tauri-build = { version = "2", features = [] }

[dependencies]
tauri = { version = "2", features = [] }
tauri-plugin-dialog = "2"
serde = { workspace = true }
serde_json = "1"
anyhow = { workspace = true }
tokio = { version = "1", features = ["full"] }
dirs = "5"
thiserror = { workspace = true }

[features]
default = ["custom-protocol"]
custom-protocol = ["tauri/custom-protocol"]
```

- [ ] **Step 3: Write `tauri.conf.json`**

`crates/boardghost-launcher/src-tauri/tauri.conf.json`:

```json
{
  "$schema": "../node_modules/@tauri-apps/cli/config.schema.json",
  "productName": "BoardGhost",
  "version": "0.1.0",
  "identifier": "io.boardghost.launcher",
  "build": {
    "beforeDevCommand": "npm run dev",
    "beforeBuildCommand": "npm run build",
    "devUrl": "http://localhost:1420",
    "frontendDist": "../dist"
  },
  "app": {
    "windows": [
      {
        "title": "BoardGhost",
        "width": 1000,
        "height": 640,
        "minWidth": 700,
        "minHeight": 480,
        "resizable": true
      }
    ],
    "security": {
      "csp": "default-src 'self'; style-src 'self' 'unsafe-inline'"
    }
  },
  "bundle": {
    "active": true,
    "targets": "all",
    "icon": ["icons/32x32.png", "icons/128x128.png", "icons/icon.png"]
  },
  "plugins": {}
}
```

- [ ] **Step 4: Write `build.rs`**

`crates/boardghost-launcher/src-tauri/build.rs`:

```rust
fn main() {
    tauri_build::build();
}
```

- [ ] **Step 5: Write minimal `src/main.rs`**

`crates/boardghost-launcher/src-tauri/src/main.rs`:

```rust
// Prevent additional console window on Windows in release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![])  // commands wired in later tasks
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
```

- [ ] **Step 6: Create placeholder icons**

Tauri requires icon files at the paths declared in `tauri.conf.json`. Generate solid-color PNG placeholders:

```bash
# 32x32 solid black PNG
python3 -c "
from struct import pack
import zlib, os
def png(w, h, path):
    raw = b''.join(b'\x00' + b'\x00\x00\x00\xff' * w for _ in range(h))
    def chunk(t, d):
        return pack('>I', len(d)) + t + d + pack('>I', zlib.crc32(t + d) & 0xffffffff)
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    idat = zlib.compress(raw)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(sig + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', b''))
png(32, 32, 'crates/boardghost-launcher/src-tauri/icons/32x32.png')
png(128, 128, 'crates/boardghost-launcher/src-tauri/icons/128x128.png')
png(256, 256, 'crates/boardghost-launcher/src-tauri/icons/icon.png')
"
file crates/boardghost-launcher/src-tauri/icons/*.png  # verify all are valid PNGs
```

A real icon design is M2+; this just satisfies Tauri's bundle requirement.

- [ ] **Step 7: Install tauri-cli + verify build**

```bash
cargo install tauri-cli --version "^2.0" --locked
cargo tauri --version  # confirm v2.x
cd crates/boardghost-launcher
cargo tauri build --debug  # full build to verify everything wires up
```

This is a slow first build (compiles Tauri + dependencies, ~5–10 min on a clean machine). If you only want a fast check that the project structure is valid, use `cargo build -p boardghost-launcher --manifest-path src-tauri/Cargo.toml` instead.

Expected: a `boardghost-launcher` binary appears under `src-tauri/target/debug/`.

- [ ] **Step 8: Commit**

```bash
git add crates/boardghost-launcher/src-tauri Cargo.toml
git commit -m "feat(launcher): add Tauri v2 backend skeleton"
```

---

## Task 3: Project state persistence

**Files:**
- Create: `crates/boardghost-launcher/src-tauri/src/projects.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs` (declare module)
- Create: `crates/boardghost-launcher/src-tauri/tests/projects_test.rs` (integration test on the projects module)

- [ ] **Step 1: Write the failing test**

`crates/boardghost-launcher/src-tauri/tests/projects_test.rs`:

```rust
use boardghost_launcher::projects::{ProjectStore, ProjectEntry};
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn empty_store_has_no_projects() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let store = ProjectStore::load(&path).expect("load missing file → empty");
    assert!(store.recent().is_empty());
}

#[test]
fn add_and_persist_roundtrip() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/home/u/code/demo"),
        board: "ili9488_esp32s3_sim".to_string(),
        last_used: 1716840000,
    });
    store.save(&path).unwrap();

    let reloaded = ProjectStore::load(&path).unwrap();
    assert_eq!(reloaded.recent().len(), 1);
    assert_eq!(reloaded.recent()[0].path, PathBuf::from("/home/u/code/demo"));
    assert_eq!(reloaded.recent()[0].board, "ili9488_esp32s3_sim");
}

#[test]
fn add_or_update_overwrites_same_path() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/p"),
        board: "a".into(),
        last_used: 1,
    });
    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/p"),
        board: "b".into(),
        last_used: 2,
    });

    assert_eq!(store.recent().len(), 1);
    assert_eq!(store.recent()[0].board, "b");
    assert_eq!(store.recent()[0].last_used, 2);
}

#[test]
fn recent_sorted_by_last_used_desc() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry { path: "/a".into(), board: "x".into(), last_used: 100 });
    store.add_or_update(ProjectEntry { path: "/b".into(), board: "x".into(), last_used: 300 });
    store.add_or_update(ProjectEntry { path: "/c".into(), board: "x".into(), last_used: 200 });

    let r = store.recent();
    assert_eq!(r[0].path.to_str().unwrap(), "/b");
    assert_eq!(r[1].path.to_str().unwrap(), "/c");
    assert_eq!(r[2].path.to_str().unwrap(), "/a");
}
```

Add `tempfile.workspace = true` and `dev-dependencies` block to `crates/boardghost-launcher/src-tauri/Cargo.toml`:

```toml
[dev-dependencies]
tempfile = { workspace = true }
```

Also: the test imports from `boardghost_launcher::projects::...`. For that to resolve, `src/main.rs` is the binary entry — we also need a `lib.rs`. Add this to Cargo.toml:

```toml
[lib]
name = "boardghost_launcher"
path = "src/lib.rs"
```

And create `crates/boardghost-launcher/src-tauri/src/lib.rs`:

```rust
pub mod projects;
```

- [ ] **Step 2: Run the failing test**

```bash
cargo test -p boardghost-launcher
```

Expected: FAIL — `projects` module doesn't exist.

- [ ] **Step 3: Implement `src/projects.rs`**

`crates/boardghost-launcher/src-tauri/src/projects.rs`:

```rust
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectEntry {
    pub path: PathBuf,
    pub board: String,
    pub last_used: u64,   // unix seconds
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ProjectStore {
    #[serde(default)]
    entries: Vec<ProjectEntry>,
}

impl ProjectStore {
    pub fn load(path: &Path) -> anyhow::Result<Self> {
        if !path.exists() {
            return Ok(Self::default());
        }
        let text = std::fs::read_to_string(path)?;
        // Tolerate a corrupt or empty file by returning an empty store instead
        // of erroring — the launcher should never refuse to start because of a
        // bad projects.json. Better to log and recover.
        Ok(serde_json::from_str(&text).unwrap_or_default())
    }

    pub fn save(&self, path: &Path) -> anyhow::Result<()> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let text = serde_json::to_string_pretty(self)?;
        std::fs::write(path, text)?;
        Ok(())
    }

    pub fn add_or_update(&mut self, entry: ProjectEntry) {
        if let Some(slot) = self.entries.iter_mut().find(|e| e.path == entry.path) {
            *slot = entry;
        } else {
            self.entries.push(entry);
        }
    }

    pub fn recent(&self) -> Vec<ProjectEntry> {
        let mut out = self.entries.clone();
        out.sort_by(|a, b| b.last_used.cmp(&a.last_used));
        out
    }
}
```

- [ ] **Step 4: Re-run the tests**

```bash
cargo test -p boardghost-launcher
```

Expected: all 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-launcher/src-tauri
git commit -m "feat(launcher): projects.json store with load/save/upsert"
```

---

## Task 4: AppState + Tauri command stubs

**Files:**
- Create: `crates/boardghost-launcher/src-tauri/src/state.rs`
- Create: `crates/boardghost-launcher/src-tauri/src/commands.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/lib.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs`

- [ ] **Step 1: Write `state.rs`**

`crates/boardghost-launcher/src-tauri/src/state.rs`:

```rust
use std::path::PathBuf;
use std::sync::Mutex;
use tokio::process::Child;

use crate::projects::ProjectStore;

/// Shared application state held in Tauri's State<'_>.
pub struct AppState {
    /// Path to projects.json — fixed at startup so tests can override.
    pub projects_path: PathBuf,
    /// In-memory copy of the project store.
    pub store:         Mutex<ProjectStore>,
    /// Currently running sketch child (if any). Killed on `stop`.
    pub running:       Mutex<Option<Child>>,
}

impl AppState {
    pub fn new(projects_path: PathBuf) -> anyhow::Result<Self> {
        let store = ProjectStore::load(&projects_path).unwrap_or_default();
        Ok(Self {
            projects_path,
            store:   Mutex::new(store),
            running: Mutex::new(None),
        })
    }
}
```

- [ ] **Step 2: Write `commands.rs` with stub handlers**

`crates/boardghost-launcher/src-tauri/src/commands.rs`:

```rust
use std::path::PathBuf;
use tauri::State;

use crate::projects::ProjectEntry;
use crate::state::AppState;

#[derive(serde::Serialize)]
pub struct ProjectSummary {
    pub path:      PathBuf,
    pub board:     String,
    pub last_used: u64,
}

impl From<ProjectEntry> for ProjectSummary {
    fn from(e: ProjectEntry) -> Self {
        Self { path: e.path, board: e.board, last_used: e.last_used }
    }
}

#[tauri::command]
pub fn list_recent_projects(state: State<'_, AppState>) -> Vec<ProjectSummary> {
    let store = state.store.lock().unwrap();
    store.recent().into_iter().map(Into::into).collect()
}

#[tauri::command]
pub fn add_project(
    state: State<'_, AppState>,
    path:  PathBuf,
    board: String,
) -> Result<(), String> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);

    {
        let mut store = state.store.lock().unwrap();
        store.add_or_update(ProjectEntry { path, board, last_used: now });
        store.save(&state.projects_path).map_err(|e| e.to_string())?;
    }
    Ok(())
}

// list_boards, build_and_run, stop arrive in later tasks (5, 7, 8).
```

- [ ] **Step 3: Update `src/lib.rs`**

`crates/boardghost-launcher/src-tauri/src/lib.rs`:

```rust
pub mod commands;
pub mod projects;
pub mod state;
```

- [ ] **Step 4: Wire state + commands into `src/main.rs`**

```rust
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
```

- [ ] **Step 5: Verify build**

```bash
cargo build -p boardghost-launcher
```

Expected: clean build. No new tests yet (Task 3's tests still pass).

- [ ] **Step 6: Commit**

```bash
git add crates/boardghost-launcher/src-tauri
git commit -m "feat(launcher): AppState + list_recent_projects/add_project commands"
```

---

## Task 5: list_boards command — shell out to `boardghost list-boards`

**Files:**
- Modify: `crates/boardghost-launcher/src-tauri/src/commands.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs` (register new command)
- Create: `crates/boardghost-launcher/src-tauri/tests/list_boards_test.rs`

- [ ] **Step 1: Write the test**

The test exercises a pure helper `parse_list_boards_output(stdout)` so it doesn't actually need to spawn `boardghost`. We do the IO in a thin wrapper.

`crates/boardghost-launcher/src-tauri/tests/list_boards_test.rs`:

```rust
use boardghost_launcher::commands::parse_list_boards_output;

#[test]
fn parses_two_line_output() {
    let stdout = "  ili9488_esp32s3_sim           ESP32-S3 with ILI9488 480x320 SPI display\n\
                  ssd1306_uno_sim               Arduino Uno with SSD1306 128x64 mono OLED\n";
    let boards = parse_list_boards_output(stdout);
    assert_eq!(boards.len(), 2);
    assert_eq!(boards[0].name, "ili9488_esp32s3_sim");
    assert_eq!(boards[0].description, "ESP32-S3 with ILI9488 480x320 SPI display");
    assert_eq!(boards[1].name, "ssd1306_uno_sim");
}

#[test]
fn skips_blank_lines() {
    let stdout = "\n  foo  bar baz\n\n";
    let boards = parse_list_boards_output(stdout);
    assert_eq!(boards.len(), 1);
    assert_eq!(boards[0].name, "foo");
    assert_eq!(boards[0].description, "bar baz");
}
```

- [ ] **Step 2: Add the parser + command to `commands.rs`**

Append to `crates/boardghost-launcher/src-tauri/src/commands.rs`:

```rust
#[derive(serde::Serialize, Debug)]
pub struct BoardSummary {
    pub name:        String,
    pub description: String,
}

pub fn parse_list_boards_output(stdout: &str) -> Vec<BoardSummary> {
    stdout
        .lines()
        .map(|l| l.trim())
        .filter(|l| !l.is_empty())
        .filter_map(|l| {
            let mut iter = l.splitn(2, char::is_whitespace);
            let name = iter.next()?.to_string();
            let desc = iter.next()?.trim().to_string();
            Some(BoardSummary { name, description: desc })
        })
        .collect()
}

#[tauri::command]
pub async fn list_boards() -> Result<Vec<BoardSummary>, String> {
    let out = tokio::process::Command::new("boardghost")
        .arg("list-boards")
        .output()
        .await
        .map_err(|e| format!("could not invoke boardghost: {e}"))?;
    if !out.status.success() {
        return Err(format!(
            "boardghost list-boards exited {}: {}",
            out.status,
            String::from_utf8_lossy(&out.stderr)
        ));
    }
    Ok(parse_list_boards_output(&String::from_utf8_lossy(&out.stdout)))
}
```

- [ ] **Step 3: Register the command in `main.rs`**

Update the `invoke_handler` macro:

```rust
        .invoke_handler(tauri::generate_handler![
            commands::list_recent_projects,
            commands::add_project,
            commands::list_boards,
        ])
```

- [ ] **Step 4: Run tests**

```bash
cargo test -p boardghost-launcher
```

Expected: 6 tests pass (4 projects + 2 list_boards parser).

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-launcher/src-tauri
git commit -m "feat(launcher): list_boards command via shelling out to CLI"
```

---

## Task 6: runner module — spawn boardghost + stream stdout/stderr

**Files:**
- Create: `crates/boardghost-launcher/src-tauri/src/runner.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/lib.rs` (declare module)

- [ ] **Step 1: Implement `runner.rs`**

`crates/boardghost-launcher/src-tauri/src/runner.rs`:

```rust
use std::path::PathBuf;
use std::process::Stdio;
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

/// Spawn `boardghost run <project> --board <board>` as a child process.
/// stdout lines are emitted on the `serial-log` event; stderr lines on
/// the `build-log` event (which is where the CLI writes its own progress).
///
/// The returned Child is owned by the caller (typically stashed in AppState
/// so `stop` can kill it). The two reader tasks run until EOF or are
/// implicitly cancelled when the child is killed.
pub async fn spawn(
    app:     AppHandle,
    project: PathBuf,
    board:   String,
) -> anyhow::Result<Child> {
    let mut child = Command::new("boardghost")
        .arg("run")
        .arg(&project)
        .arg("--arg-board-marker")  // ensures explicit `--board` is unambiguous
        .arg("--board")
        .arg(&board)
        .env("SDL_VIDEODRIVER", "x11")  // use real X11/Wayland window
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

    if let Some(stdout) = child.stdout.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stdout);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = app.emit("serial-log", line);
            }
        });
    }

    if let Some(stderr) = child.stderr.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stderr);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = app.emit("build-log", line);
            }
        });
    }

    Ok(child)
}
```

NOTE: the `--arg-board-marker` arg in the command above was a leftover thinking-out-loud aid in my draft. REMOVE that line before committing — the actual command should just be `Command::new("boardghost").arg("run").arg(&project).arg("--board").arg(&board)`. Triple-check by reading the spawned command back from a test if you want.

Corrected spawn line set:

```rust
    let mut child = Command::new("boardghost")
        .arg("run")
        .arg(&project)
        .arg("--board")
        .arg(&board)
        .env("SDL_VIDEODRIVER", "x11")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;
```

- [ ] **Step 2: Declare module in `lib.rs`**

`crates/boardghost-launcher/src-tauri/src/lib.rs`:

```rust
pub mod commands;
pub mod projects;
pub mod runner;
pub mod state;
```

- [ ] **Step 3: Verify build**

```bash
cargo build -p boardghost-launcher
```

Expected: clean build. No new tests (runner is exercised via the build_and_run command in Task 7 and the manual smoke test in Task 12).

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher/src-tauri/src/runner.rs \
        crates/boardghost-launcher/src-tauri/src/lib.rs
git commit -m "feat(launcher): runner spawn helper streams stdout/stderr to events"
```

---

## Task 7: build_and_run command + frontend stub

**Files:**
- Modify: `crates/boardghost-launcher/src-tauri/src/commands.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs` (register command)

- [ ] **Step 1: Add `build_and_run` to `commands.rs`**

Append:

```rust
use crate::runner;
use tauri::AppHandle;

#[tauri::command]
pub async fn build_and_run(
    app:     AppHandle,
    state:   State<'_, AppState>,
    project: PathBuf,
    board:   String,
) -> Result<(), String> {
    // Kill any previous sketch first.
    {
        let mut slot = state.running.lock().unwrap();
        if let Some(mut child) = slot.take() {
            let _ = child.start_kill();
        }
    }

    let child = runner::spawn(app.clone(), project.clone(), board.clone())
        .await
        .map_err(|e| e.to_string())?;

    {
        let mut slot = state.running.lock().unwrap();
        *slot = Some(child);
    }

    // Record the project as recently used.
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    {
        let mut store = state.store.lock().unwrap();
        store.add_or_update(crate::projects::ProjectEntry { path: project, board, last_used: now });
        store.save(&state.projects_path).map_err(|e| e.to_string())?;
    }

    Ok(())
}
```

- [ ] **Step 2: Register the command in `main.rs`**

Update `invoke_handler`:

```rust
        .invoke_handler(tauri::generate_handler![
            commands::list_recent_projects,
            commands::add_project,
            commands::list_boards,
            commands::build_and_run,
        ])
```

- [ ] **Step 3: Verify build**

```bash
cargo build -p boardghost-launcher
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher/src-tauri
git commit -m "feat(launcher): build_and_run command spawns CLI + records recent"
```

---

## Task 8: stop command — graceful then forceful kill

**Files:**
- Modify: `crates/boardghost-launcher/src-tauri/src/commands.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs`

- [ ] **Step 1: Add `stop` to `commands.rs`**

Append:

```rust
#[tauri::command]
pub async fn stop(state: State<'_, AppState>) -> Result<(), String> {
    let child_opt = {
        let mut slot = state.running.lock().unwrap();
        slot.take()
    };

    let Some(mut child) = child_opt else { return Ok(()); };

    // Try graceful kill first. tokio's Child::start_kill maps to SIGKILL on
    // Unix; for SIGTERM we drop to the raw libc / unix-specific path.
    #[cfg(unix)]
    {
        if let Some(pid) = child.id() {
            unsafe { libc::kill(pid as i32, libc::SIGTERM); }
        }
    }

    // Wait up to 2 seconds.
    let wait = tokio::time::timeout(std::time::Duration::from_secs(2), child.wait()).await;
    if wait.is_err() {
        // Still alive — escalate.
        let _ = child.start_kill();
        let _ = child.wait().await;
    }
    Ok(())
}
```

Add `libc = "0.2"` to `crates/boardghost-launcher/src-tauri/Cargo.toml` dependencies.

On non-Unix, the `#[cfg(unix)]` block is skipped — `child.start_kill()` (SIGKILL on Unix, TerminateProcess on Windows) is the fallback. Tauri's primary target is desktop, so this is acceptable.

- [ ] **Step 2: Register in `main.rs`**

```rust
        .invoke_handler(tauri::generate_handler![
            commands::list_recent_projects,
            commands::add_project,
            commands::list_boards,
            commands::build_and_run,
            commands::stop,
        ])
```

- [ ] **Step 3: Verify build**

```bash
cargo build -p boardghost-launcher
```

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher
git commit -m "feat(launcher): stop command — SIGTERM with 2s grace, then SIGKILL"
```

---

## Task 9: Typed frontend API wrapper

**Files:**
- Create: `crates/boardghost-launcher/src/lib/api.ts`

- [ ] **Step 1: Write the api wrapper**

`crates/boardghost-launcher/src/lib/api.ts`:

```typescript
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { open as openDialog } from "@tauri-apps/plugin-dialog";

export interface ProjectSummary {
  path: string;
  board: string;
  last_used: number;
}

export interface BoardSummary {
  name: string;
  description: string;
}

export async function listRecentProjects(): Promise<ProjectSummary[]> {
  return invoke<ProjectSummary[]>("list_recent_projects");
}

export async function addProject(path: string, board: string): Promise<void> {
  await invoke("add_project", { path, board });
}

export async function listBoards(): Promise<BoardSummary[]> {
  return invoke<BoardSummary[]>("list_boards");
}

export async function buildAndRun(project: string, board: string): Promise<void> {
  await invoke("build_and_run", { project, board });
}

export async function stop(): Promise<void> {
  await invoke("stop");
}

export async function pickProjectDir(): Promise<string | null> {
  const result = await openDialog({ directory: true, multiple: false });
  return typeof result === "string" ? result : null;
}

// Event subscriptions. Caller MUST hold the returned UnlistenFn and call it
// on component unmount to prevent leaked listeners.
export function onBuildLog(handler: (line: string) => void): Promise<UnlistenFn> {
  return listen<string>("build-log", (e) => handler(e.payload));
}

export function onSerialLog(handler: (line: string) => void): Promise<UnlistenFn> {
  return listen<string>("serial-log", (e) => handler(e.payload));
}
```

- [ ] **Step 2: Verify type-check**

```bash
cd crates/boardghost-launcher
npm run check
cd ../..
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add crates/boardghost-launcher/src/lib/api.ts
git commit -m "feat(launcher): typed TS wrapper over Tauri commands + events"
```

---

## Task 10: ProjectList + BoardSelect + LogPanel components

**Files:**
- Create: `crates/boardghost-launcher/src/lib/ProjectList.svelte`
- Create: `crates/boardghost-launcher/src/lib/BoardSelect.svelte`
- Create: `crates/boardghost-launcher/src/lib/LogPanel.svelte`

- [ ] **Step 1: ProjectList**

`crates/boardghost-launcher/src/lib/ProjectList.svelte`:

```svelte
<script lang="ts">
  import type { ProjectSummary } from "./api";
  import { pickProjectDir } from "./api";

  let { projects, selected = $bindable() }:
    { projects: ProjectSummary[]; selected: string | null } = $props();

  async function openDialog() {
    const dir = await pickProjectDir();
    if (dir) selected = dir;
  }

  function formatRelative(unixSec: number): string {
    const ageSec = Math.floor(Date.now() / 1000) - unixSec;
    if (ageSec < 60)        return "just now";
    if (ageSec < 3600)      return `${Math.floor(ageSec / 60)}m ago`;
    if (ageSec < 86400)     return `${Math.floor(ageSec / 3600)}h ago`;
    return `${Math.floor(ageSec / 86400)}d ago`;
  }
</script>

<aside class="project-list">
  <h2>Projects</h2>
  <ul>
    {#each projects as p (p.path)}
      <li class:active={p.path === selected}>
        <button type="button" onclick={() => (selected = p.path)}>
          <div class="path">{p.path.split("/").pop()}</div>
          <div class="meta">{formatRelative(p.last_used)} · {p.board}</div>
        </button>
      </li>
    {/each}
  </ul>
  <button class="open" type="button" onclick={openDialog}>+ Open Project</button>
</aside>

<style>
  .project-list { display: flex; flex-direction: column; gap: 0.5rem; padding: 0.75rem; border-right: 1px solid #ddd; height: 100%; box-sizing: border-box; }
  h2 { margin: 0; font-size: 0.85rem; text-transform: uppercase; letter-spacing: 0.05em; color: #666; }
  ul { list-style: none; padding: 0; margin: 0; flex: 1; overflow-y: auto; }
  li { margin-bottom: 0.25rem; }
  li button { width: 100%; text-align: left; padding: 0.5rem; background: transparent; border: 1px solid transparent; border-radius: 4px; cursor: pointer; }
  li button:hover { background: rgba(0,0,0,0.04); }
  li.active button { background: rgba(0,100,255,0.08); border-color: rgba(0,100,255,0.4); }
  .path { font-weight: 600; }
  .meta { font-size: 0.75rem; color: #888; }
  .open { padding: 0.5rem; border: 1px dashed #888; border-radius: 4px; background: transparent; cursor: pointer; }
</style>
```

- [ ] **Step 2: BoardSelect**

`crates/boardghost-launcher/src/lib/BoardSelect.svelte`:

```svelte
<script lang="ts">
  import type { BoardSummary } from "./api";

  let { boards, value = $bindable() }:
    { boards: BoardSummary[]; value: string } = $props();
</script>

<label class="board-select">
  <span>Board</span>
  <select bind:value>
    {#each boards as b (b.name)}
      <option value={b.name}>{b.name} — {b.description}</option>
    {/each}
  </select>
</label>

<style>
  .board-select { display: flex; flex-direction: column; gap: 0.25rem; }
  span { font-size: 0.75rem; color: #666; }
  select { padding: 0.4rem; }
</style>
```

- [ ] **Step 3: LogPanel**

`crates/boardghost-launcher/src/lib/LogPanel.svelte`:

```svelte
<script lang="ts">
  let { title, lines }: { title: string; lines: string[] } = $props();
  let containerEl: HTMLElement | undefined = $state();

  // Auto-scroll to bottom on new lines.
  $effect(() => {
    void lines.length;  // dep
    if (containerEl) containerEl.scrollTop = containerEl.scrollHeight;
  });
</script>

<section class="log-panel">
  <header>{title}</header>
  <div bind:this={containerEl} class="lines">
    {#each lines as line, i (i)}
      <div>{line}</div>
    {/each}
  </div>
</section>

<style>
  .log-panel { display: flex; flex-direction: column; min-height: 0; }
  header { font-size: 0.75rem; font-weight: 600; padding: 0.4rem 0.5rem; background: #f3f3f3; border-top: 1px solid #ddd; border-bottom: 1px solid #ddd; }
  .lines { flex: 1; overflow-y: auto; font-family: ui-monospace, "SF Mono", Menlo, monospace; font-size: 0.78rem; padding: 0.4rem 0.5rem; background: #fafafa; min-height: 0; }
  .lines > div { white-space: pre-wrap; }
</style>
```

- [ ] **Step 4: Type-check**

```bash
cd crates/boardghost-launcher && npm run check && cd ../..
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-launcher/src/lib/ProjectList.svelte \
        crates/boardghost-launcher/src/lib/BoardSelect.svelte \
        crates/boardghost-launcher/src/lib/LogPanel.svelte
git commit -m "feat(launcher): ProjectList, BoardSelect, LogPanel components"
```

---

## Task 11: ProjectDetail (right column) + wire App.svelte

**Files:**
- Create: `crates/boardghost-launcher/src/lib/ProjectDetail.svelte`
- Replace: `crates/boardghost-launcher/src/App.svelte`

- [ ] **Step 1: ProjectDetail**

`crates/boardghost-launcher/src/lib/ProjectDetail.svelte`:

```svelte
<script lang="ts">
  import { buildAndRun, stop, type BoardSummary } from "./api";
  import BoardSelect from "./BoardSelect.svelte";
  import LogPanel from "./LogPanel.svelte";

  let { project, boards, buildLines, serialLines }:
    {
      project:     string | null;
      boards:      BoardSummary[];
      buildLines:  string[];
      serialLines: string[];
    } = $props();

  let selectedBoard = $state("");
  let running = $state(false);
  let lastError: string | null = $state(null);

  // Default the board to the first available when boards arrive.
  $effect(() => {
    if (!selectedBoard && boards.length > 0) selectedBoard = boards[0].name;
  });

  async function onRun() {
    if (!project || !selectedBoard) return;
    lastError = null;
    running = true;
    try {
      await buildAndRun(project, selectedBoard);
    } catch (e) {
      lastError = String(e);
      running = false;
    }
  }

  async function onStop() {
    try { await stop(); } finally { running = false; }
  }
</script>

<section class="detail">
  {#if project}
    <header>
      <h2>{project}</h2>
    </header>

    <div class="controls">
      <BoardSelect {boards} bind:value={selectedBoard} />
      <div class="buttons">
        <button type="button" onclick={onRun} disabled={running || !selectedBoard}>
          ▶ Build &amp; Run
        </button>
        <button type="button" onclick={onStop} disabled={!running}>
          ■ Stop
        </button>
      </div>
    </div>

    {#if lastError}
      <div class="error">Error: {lastError}</div>
    {/if}

    <div class="logs">
      <LogPanel title="Build output" lines={buildLines} />
      <LogPanel title="Serial monitor" lines={serialLines} />
    </div>
  {:else}
    <div class="empty">
      Select or open a project to begin.
    </div>
  {/if}
</section>

<style>
  .detail { display: flex; flex-direction: column; height: 100%; padding: 0.75rem; gap: 0.5rem; min-height: 0; }
  header h2 { font-size: 0.9rem; font-family: ui-monospace, monospace; margin: 0; word-break: break-all; }
  .controls { display: flex; gap: 1rem; align-items: end; }
  .buttons { display: flex; gap: 0.5rem; }
  .buttons button { padding: 0.5rem 1rem; font-weight: 600; }
  .error { padding: 0.5rem; background: #fee; border: 1px solid #fcc; border-radius: 4px; font-size: 0.85rem; }
  .logs { display: grid; grid-template-rows: 1fr 1fr; gap: 0.5rem; flex: 1; min-height: 0; }
  .empty { display: flex; align-items: center; justify-content: center; height: 100%; color: #888; }
</style>
```

- [ ] **Step 2: Replace App.svelte**

`crates/boardghost-launcher/src/App.svelte`:

```svelte
<script lang="ts">
  import { onMount } from "svelte";
  import {
    listRecentProjects, listBoards, onBuildLog, onSerialLog,
    type ProjectSummary, type BoardSummary,
  } from "./lib/api";
  import ProjectList   from "./lib/ProjectList.svelte";
  import ProjectDetail from "./lib/ProjectDetail.svelte";

  let projects:    ProjectSummary[] = $state([]);
  let boards:      BoardSummary[]   = $state([]);
  let selected:    string | null    = $state(null);
  let buildLines:  string[]         = $state([]);
  let serialLines: string[]         = $state([]);

  onMount(async () => {
    projects = await listRecentProjects();
    try {
      boards = await listBoards();
    } catch (e) {
      // Surface in build panel as a startup error.
      buildLines = [`Could not list boards: ${e}`];
    }

    const unBuild  = await onBuildLog((line)  => buildLines  = [...buildLines, line]);
    const unSerial = await onSerialLog((line) => serialLines = [...serialLines, line]);

    return () => { unBuild(); unSerial(); };
  });
</script>

<div class="layout">
  <ProjectList {projects} bind:selected />
  <ProjectDetail project={selected} {boards} {buildLines} {serialLines} />
</div>

<style>
  .layout { display: grid; grid-template-columns: 240px 1fr; height: 100vh; min-height: 0; }
</style>
```

- [ ] **Step 3: Type-check + production build**

```bash
cd crates/boardghost-launcher
npm run check
npm run build
cd ../..
```

Expected: clean build to `dist/`.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher/src/lib/ProjectDetail.svelte \
        crates/boardghost-launcher/src/App.svelte
git commit -m "feat(launcher): ProjectDetail + App layout (project list | build/run/logs)"
```

---

## Task 12: Manual smoke test

This task has no code. It validates the launcher end-to-end interactively.

- [ ] **Step 1: Build the CLI in release mode and put on PATH**

```bash
cargo build --release -p boardghost-cli
sudo install -m 0755 target/release/boardghost /usr/local/bin/boardghost
# OR add target/release to PATH for the current shell.
which boardghost
boardghost list-boards   # confirm CLI works standalone
```

- [ ] **Step 2: Run the launcher in dev mode**

```bash
cd crates/boardghost-launcher
cargo tauri dev
```

Wait for the window to appear (~10s on first run, faster on subsequent).

- [ ] **Step 3: Manually verify the full flow**

Verify each:

- [ ] Window opens, two columns visible, board dropdown is populated with `ili9488_esp32s3_sim` and `ssd1306_uno_sim`.
- [ ] Click "+ Open Project" → file dialog opens.
- [ ] Select `<repo>/examples/lvgl_hello_ili9488` → project path appears in the right column.
- [ ] Select `ili9488_esp32s3_sim` in the dropdown.
- [ ] Click "▶ Build & Run".
- [ ] Build output panel shows CLI progress lines (`→ Sketch:`, `→ Preprocessing...`, `→ Compiling...`).
- [ ] After the build, a separate SDL window opens showing the LVGL button.
- [ ] Serial monitor panel shows `lvgl_hello_ili9488 started` and `frame N` lines.
- [ ] Click the LVGL button → `button clicked, total=N` appears in the serial panel.
- [ ] Click "■ Stop" → sketch process terminates, SDL window closes, button states reset.
- [ ] Close the launcher window — both processes (launcher + any orphaned child) exit cleanly. `pgrep -f boardghost` should return empty after a second.
- [ ] Restart the launcher → recent project list contains the project you just opened, with its board selection persisted.

If ANY step fails, write down the failure mode, fix it, repeat.

- [ ] **Step 4: Document the smoke test outcome**

There's no commit for this task itself, but if you fixed bugs during testing, commit them with messages like `fix(launcher): <what>`.

---

## Task 13: Production build verification

**Files:** none.

- [ ] **Step 1: Build the production launcher binary**

```bash
cd crates/boardghost-launcher
cargo tauri build
```

This produces a bundle under `src-tauri/target/release/bundle/`. On Linux this will be an AppImage and/or .deb file.

- [ ] **Step 2: Run the bundled binary directly**

```bash
ls src-tauri/target/release/bundle/
# Pick the platform-appropriate artifact, e.g. for Linux:
./src-tauri/target/release/boardghost-launcher
```

The launcher should open identical to the dev-mode window.

- [ ] **Step 3: Confirm the bundle size is reasonable**

```bash
du -sh src-tauri/target/release/bundle/*
```

Tauri v2 bundles typically come in around 8–15 MB on Linux. If it's >100 MB, something is wrong (probably bundling node_modules or build artifacts).

- [ ] **Step 4: Note (no commit needed for this task)**

The bundle artifacts live under `src-tauri/target/release/bundle/` which is already gitignored by the top-level `target/` rule. Nothing to commit here.

---

## Task 14: Documentation — launcher section in README + getting-started

**Files:**
- Modify: `README.md`
- Modify: `docs/getting-started.md`

- [ ] **Step 1: Add launcher status to README**

In `README.md`, find the "Status:" line near the top and update it:

```markdown
**Status:** M1 shipping. Engine + CLI tagged at `m1a-engine`. Launcher (M1.B) building.
```

And replace the existing "What works (M1.A)" section header:

```markdown
## What works

- ILI9488 480×320 and SSD1306 128×64 simulated panels (LovyanGFX + LVGL)
- Mouse → touch input
- `Serial.print` capture
- CLI: `boardghost {list-boards|doctor|build|run}`
- Tauri desktop launcher (`cargo tauri dev` in `crates/boardghost-launcher`)
- Headless mode (`SDL_VIDEODRIVER=dummy`) for CI
```

- [ ] **Step 2: Append launcher section to getting-started.md**

Append after the existing sections:

```markdown
## 8. Using the desktop launcher

The launcher is a Tauri + Svelte app over the same CLI. To run it in dev mode:

```bash
cd crates/boardghost-launcher
cargo tauri dev
```

To build a distributable binary:

```bash
cd crates/boardghost-launcher
cargo tauri build
ls src-tauri/target/release/bundle/
```

The launcher requires `boardghost` to be on `PATH`. Either install it system-wide:

```bash
cargo install --path crates/boardghost-cli
```

…or symlink the dev binary:

```bash
sudo ln -sf "$PWD/target/release/boardghost" /usr/local/bin/boardghost
```
```

- [ ] **Step 3: Commit**

```bash
git add README.md docs/getting-started.md
git commit -m "docs: add launcher (M1.B) section to README and getting-started"
```

---

## Task 15: CI — launcher build job

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add a launcher job to ci.yml**

Append after the existing `build-and-test` job:

```yaml
  launcher-build:
    runs-on: ubuntu-latest
    needs: build-and-test
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install Tauri host deps
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libwebkit2gtk-4.1-dev \
            libappindicator3-dev \
            librsvg2-dev \
            patchelf \
            libsdl2-dev

      - uses: actions/setup-node@v4
        with:
          node-version: '20'

      - uses: dtolnay/rust-toolchain@stable

      - name: Install npm deps
        working-directory: crates/boardghost-launcher
        run: npm ci

      - name: Type-check frontend
        working-directory: crates/boardghost-launcher
        run: npm run check

      - name: Cargo test (launcher backend)
        run: cargo test -p boardghost-launcher

      - name: Build launcher (debug, no bundle)
        working-directory: crates/boardghost-launcher
        run: cargo tauri build --debug --no-bundle
```

The `--no-bundle` flag skips AppImage/deb generation, which is slow and not needed for CI verification. Keeping `--debug` because release-mode link-time optimization adds 5–10 minutes.

Note: `npm ci` requires a `package-lock.json`. If one wasn't created during Task 1's `npm install`, generate it now: `cd crates/boardghost-launcher && npm install && git add package-lock.json && git commit -m "chore(launcher): add package-lock.json"`.

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci.yml
# include package-lock.json if not yet committed
git add crates/boardghost-launcher/package-lock.json 2>/dev/null || true
git commit -m "ci: add launcher build job"
```

- [ ] **Step 3: Push and watch CI**

```bash
git push
gh run watch
```

Both `build-and-test` and `launcher-build` jobs must pass.

- [ ] **Step 4: Tag the milestone**

After CI is green:

```bash
git tag -a m1-launcher -m "BoardGhost M1.B — Tauri launcher"
git push --tags
```

---

## Self-review

Run this checklist against the spec (§6 of the design doc) and the predecessor plan.

**Spec §6 coverage:**

| Spec line | Task |
|---|---|
| §6.1 Two-column layout, project list + detail + logs | Tasks 10, 11 |
| §6.1 TFT in separate SDL window (sketch owns it) | Inherited from M1.A; no work needed |
| §6.2 Persist `~/.config/boardghost/projects.json` | Task 3 |
| §6.2 Spawn `boardghost run` via `tokio::process::Command` | Task 6 |
| §6.2 Forward log lines via Tauri events (`build-log`, `serial-log`) | Task 6 |
| §6.2 Commands: `open_project`, `build_and_run`, `stop`, `list_boards` | Tasks 4, 5, 7, 8 (note: `open_project` is in the frontend via `pickProjectDir` + `addProject` — same effect) |
| §6.3 No demuxing needed (build and serial are separate streams) | Task 6 (stdout = serial, stderr = build) |
| §6.4 Lifecycle: kill on stop (SIGTERM → SIGKILL after 2 s) | Task 8 |
| §6.4 Exit-code reporting | Surfaced in Task 11's `lastError` (process spawn failures); child non-zero exits surface as serial-log going quiet — the user notices via the missing output |
| §6.5 NOT in M1: editor, syntax highlighting, project wizard, multi-sketch, screenshot UI | Respected — none of these are built |

**Placeholder scan:** I verified Task 6 Step 1 has a draft `--arg-board-marker` artifact called out with a "REMOVE" instruction; corrected snippet directly below it. Engineer should commit the corrected version.

**Type consistency:**

- `ProjectEntry { path, board, last_used }` defined Task 3, used Task 4 ✓
- `ProjectSummary { path, board, last_used }` (camelCase fields in TS via `serde` default) — Rust uses snake_case, TS interface mirrors. Verified in Task 9.
- `BoardSummary { name, description }` defined Task 5, used Tasks 9, 10 ✓
- Command names match across `commands.rs` and `api.ts`: `list_recent_projects`, `add_project`, `list_boards`, `build_and_run`, `stop` ✓
- Event names: `build-log`, `serial-log` — used in runner.rs (emit) and api.ts (listen) ✓

**Open items deferred to execution:**

- Exact Tauri v2 minor version pin (handled by `^2` semver in Cargo.toml; CI will lock via Cargo.lock)
- Whether `npm ci` works on first push (Task 15 handles by ensuring lockfile exists)
- Icon design (placeholders in Task 2; real icon is M2)

---

**End of plan.**
