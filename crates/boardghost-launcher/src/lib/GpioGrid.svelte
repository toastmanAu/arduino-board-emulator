<script lang="ts">
  // Tracks the most recent state of every pin we've seen activity on.
  // Pins not yet touched render as "unused" (greyed).
  interface PinState {
    mode:    "INPUT" | "OUTPUT" | "INPUT_PULLUP" | "OTHER";
    digital: 0 | 1 | null;
    pwm:     number | null;
  }

  let { events }: { events: string[] } = $props();
  let pins: Record<number, PinState> = $state({});

  // Parse the gpio-log payload — format is documented in sim_runtime.cpp:
  //   "mode <pin> <MODE_NAME>"
  //   "write <pin> <0|1>"
  //   "pwm <pin> <value>"
  function applyEvent(line: string) {
    const parts = line.split(" ");
    if (parts.length < 3) return;
    const op  = parts[0];
    const pin = parseInt(parts[1], 10);
    if (Number.isNaN(pin)) return;

    const current: PinState = pins[pin] ?? { mode: "OTHER", digital: null, pwm: null };
    if (op === "mode") {
      const m = parts[2];
      if (m === "INPUT" || m === "OUTPUT" || m === "INPUT_PULLUP" || m === "OTHER") {
        current.mode = m;
      }
    } else if (op === "write") {
      const v = parseInt(parts[2], 10);
      current.digital = v === 1 ? 1 : 0;
    } else if (op === "pwm") {
      current.pwm = parseInt(parts[2], 10);
    }
    pins[pin] = current;
  }

  $effect(() => {
    // events is append-only; reapply the last entry on each change.
    // Simple and correct without keeping a separate index.
    if (events.length === 0) return;
    applyEvent(events[events.length - 1]);
  });

  function fill(p: PinState): string {
    if (p.digital === 1) return "#3ec46d";     // green = HIGH
    if (p.digital === 0) return "#444";        // dark = LOW
    if (p.pwm !== null)  return `hsl(40, 80%, ${30 + (p.pwm / 255) * 50}%)`;
    return "#1c1c1c";                          // unused
  }

  // Visible pin window: 0..47 covers most ESP32 / Arduino variants.
  const PIN_COUNT = 48;
</script>

<section class="gpio">
  <header>GPIO</header>
  <div class="grid">
    {#each Array(PIN_COUNT) as _, n}
      {@const p = pins[n]}
      <div
        class="cell"
        title={p ? `pin ${n} mode=${p.mode} digital=${p.digital ?? "-"} pwm=${p.pwm ?? "-"}` : `pin ${n} (untouched)`}
        style:background={p ? fill(p) : "#1c1c1c"}
      >
        {n}
      </div>
    {/each}
  </div>
</section>

<style>
  .gpio { display: flex; flex-direction: column; min-height: 0; }
  header { font-size: 0.75rem; font-weight: 600; padding: 0.4rem 0.5rem; background: #f3f3f3; border-top: 1px solid #ddd; border-bottom: 1px solid #ddd; }
  .grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 2px; padding: 0.4rem; background: #fafafa; }
  .cell {
    font-family: ui-monospace, "SF Mono", Menlo, monospace;
    font-size: 0.65rem;
    color: #eee;
    padding: 0.4rem 0;
    text-align: center;
    border-radius: 2px;
    cursor: default;
  }
</style>
