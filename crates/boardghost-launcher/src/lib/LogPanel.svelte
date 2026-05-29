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
  .log-panel { display: flex; flex-direction: column; min-height: 0; overflow: hidden; }
  header { flex: 0 0 auto; font-size: 0.75rem; font-weight: 600; padding: 0.4rem 0.5rem; background: #f3f3f3; border-top: 1px solid #ddd; border-bottom: 1px solid #ddd; }
  .lines { flex: 1 1 0; min-height: 0; overflow-y: auto; overflow-x: auto; font-family: ui-monospace, "SF Mono", Menlo, monospace; font-size: 0.78rem; padding: 0.4rem 0.5rem; background: #fafafa; }
  .lines > div { white-space: pre-wrap; word-break: break-all; }
</style>
