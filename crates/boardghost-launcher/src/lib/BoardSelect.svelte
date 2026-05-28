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
