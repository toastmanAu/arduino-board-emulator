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

export function onGpioLog(handler: (line: string) => void): Promise<UnlistenFn> {
  return listen<string>("gpio-log", (e) => handler(e.payload));
}

export async function screenshot(): Promise<string> {
  return invoke<string>("screenshot");
}
