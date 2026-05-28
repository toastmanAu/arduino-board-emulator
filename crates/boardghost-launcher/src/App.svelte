<script lang="ts">
  import { onMount } from "svelte";
  import {
    listRecentProjects, listBoards, onBuildLog, onSerialLog, onGpioLog,
    type ProjectSummary, type BoardSummary,
  } from "./lib/api";
  import ProjectList   from "./lib/ProjectList.svelte";
  import ProjectDetail from "./lib/ProjectDetail.svelte";

  let projects:    ProjectSummary[] = $state([]);
  let boards:      BoardSummary[]   = $state([]);
  let selected:    string | null    = $state(null);
  let buildLines:  string[]         = $state([]);
  let serialLines: string[]         = $state([]);
  let gpioLines:   string[]         = $state([]);

  let unBuild:  (() => void) | null = null;
  let unSerial: (() => void) | null = null;
  let unGpio:   (() => void) | null = null;

  onMount(async () => {
    projects = await listRecentProjects();
    try {
      boards = await listBoards();
    } catch (e) {
      buildLines = [`Could not list boards: ${e}`];
    }

    unBuild  = await onBuildLog((line)  => buildLines  = [...buildLines,  line]);
    unSerial = await onSerialLog((line) => serialLines = [...serialLines, line]);
    unGpio   = await onGpioLog((line)   => gpioLines   = [...gpioLines,   line]);
  });

  $effect(() => {
    return () => { unBuild?.(); unSerial?.(); unGpio?.(); };
  });
</script>

<div class="layout">
  <ProjectList {projects} bind:selected />
  <ProjectDetail project={selected} {boards} {buildLines} {serialLines} {gpioLines} />
</div>

<style>
  .layout { display: grid; grid-template-columns: 240px 1fr; height: 100vh; min-height: 0; }
</style>
