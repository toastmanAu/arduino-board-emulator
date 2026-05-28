<script lang="ts">
  import { buildAndRun, stop, screenshot, type BoardSummary } from "./api";
  import BoardSelect from "./BoardSelect.svelte";
  import LogPanel    from "./LogPanel.svelte";
  import GpioGrid    from "./GpioGrid.svelte";

  let { project, boards, buildLines, serialLines, gpioLines }:
    {
      project:     string | null;
      boards:      BoardSummary[];
      buildLines:  string[];
      serialLines: string[];
      gpioLines:   string[];
    } = $props();

  let selectedBoard = $state("");
  let running       = $state(false);
  let lastError: string | null = $state(null);
  let lastScreenshot: string | null = $state(null);

  $effect(() => {
    if (!selectedBoard && boards.length > 0) selectedBoard = boards[0].name;
  });

  async function onRun() {
    if (!project || !selectedBoard) return;
    lastError = null;
    running   = true;
    try {
      await buildAndRun(project, selectedBoard);
    } catch (e) {
      lastError = String(e);
      running   = false;
    }
  }

  async function onStop() {
    try { await stop(); } finally { running = false; }
  }

  async function onScreenshot() {
    lastError = null;
    try {
      lastScreenshot = await screenshot();
    } catch (e) {
      lastError = `Screenshot failed: ${e}`;
    }
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
        <button type="button" onclick={onScreenshot} disabled={!running}>
          📷 Screenshot
        </button>
      </div>
    </div>

    {#if lastError}
      <div class="error">Error: {lastError}</div>
    {/if}

    {#if lastScreenshot}
      <div class="info">Screenshot saved: {lastScreenshot}</div>
    {/if}

    <div class="panes">
      <LogPanel title="Build output" lines={buildLines} />
      <LogPanel title="Serial monitor" lines={serialLines} />
      <GpioGrid events={gpioLines} />
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
  .info { padding: 0.5rem; background: #efe; border: 1px solid #cfc; border-radius: 4px; font-size: 0.85rem; }
  .panes { display: grid; grid-template-rows: 1fr 1fr auto; gap: 0.5rem; flex: 1; min-height: 0; }
  .empty { display: flex; align-items: center; justify-content: center; height: 100%; color: #888; }
</style>
