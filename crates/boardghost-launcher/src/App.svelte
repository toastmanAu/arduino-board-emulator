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

  let unBuild: (() => void) | undefined;
  let unSerial: (() => void) | undefined;

  onMount(async () => {
    projects = await listRecentProjects();
    try {
      boards = await listBoards();
    } catch (e) {
      // Surface in build panel as a startup error.
      buildLines = [`Could not list boards: ${e}`];
    }

    unBuild  = await onBuildLog((line)  => buildLines  = [...buildLines, line]);
    unSerial = await onSerialLog((line) => serialLines = [...serialLines, line]);
  });

  $effect.pre(() => {
    return () => {
      unBuild?.();
      unSerial?.();
    };
  });
</script>

<div class="layout">
  <ProjectList {projects} bind:selected />
  <ProjectDetail project={selected} {boards} {buildLines} {serialLines} />
</div>

<style>
  .layout { display: grid; grid-template-columns: 240px 1fr; height: 100vh; min-height: 0; }
</style>
