<script lang="ts">
  import type { RunOptions } from "./api";

  let { options = $bindable() }: { options: RunOptions } = $props();
</script>

<section class="run-settings">
  <header>Run options</header>

  <div class="row">
    <label>
      <span>Network</span>
      <select bind:value={options.net_mode}>
        <option value="">default (fake)</option>
        <option value="fake">fake — synthetic 200 OK, empty body</option>
        <option value="fail">fail — connection refused</option>
        <option value="real">real — libcurl, hits the actual URL</option>
      </select>
    </label>

    <label class="checkbox">
      <input type="checkbox" bind:checked={options.auto_touch_cal} />
      <span>Auto touch calibration</span>
    </label>
  </div>

  <div class="row">
    <label class="grow">
      <span>Scripted touches (screen coords) <code>t_ms:x,y;…</code></span>
      <input
        type="text"
        placeholder="3000:160,125;6000:50,300"
        bind:value={options.sim_touches_screen}
      />
    </label>
    <label class="narrow">
      <span>Screenshot delay (ms)</span>
      <input
        type="number"
        min="0"
        step="500"
        placeholder="2000"
        bind:value={options.screenshot_delay_ms}
      />
    </label>
  </div>

  <div class="row">
    <label class="checkbox">
      <input type="checkbox" bind:checked={options.mirror} />
      <span>Mirror to LAN (a token + pairing URL are printed to the log)</span>
    </label>
  </div>
</section>

<style>
  .run-settings {
    border: 1px solid #ddd;
    border-radius: 4px;
    padding: 0.5rem 0.75rem;
    background: #fbfbfb;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  header {
    font-size: 0.75rem;
    font-weight: 600;
    color: #555;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.6rem;
    align-items: end;
  }
  label {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    font-size: 0.78rem;
  }
  label span {
    color: #444;
  }
  label code {
    color: #666;
    font-family: ui-monospace, "SF Mono", Menlo, monospace;
    font-size: 0.72rem;
  }
  label.checkbox {
    flex-direction: row;
    align-items: center;
    gap: 0.4rem;
    padding-bottom: 0.4rem;
  }
  label.grow {
    flex: 1 1 320px;
    min-width: 240px;
  }
  label.narrow {
    flex: 0 0 130px;
  }
  input[type="text"],
  input[type="number"],
  select {
    padding: 0.3rem 0.4rem;
    font-size: 0.82rem;
    border: 1px solid #ccc;
    border-radius: 3px;
    background: white;
  }
  input[type="text"] {
    font-family: ui-monospace, "SF Mono", Menlo, monospace;
  }
</style>
