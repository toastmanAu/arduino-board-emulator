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
