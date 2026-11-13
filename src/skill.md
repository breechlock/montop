---
name: vitals
description: |
  See what a Linux machine is doing with Vitals: CPU, memory, disks, network,
  and which apps and processes are using them. Use for ANY question about why
  the computer is slow, hot, loud, out of memory, or out of disk space, what is
  using the CPU, memory, disk, or network, or whether something is running.
---

# Vitals

`vitals` reads the kernel's counters and groups processes into the apps and
services they belong to, using systemd's cgroups: a browser's forty helpers are
one app, and a terminal's shells and builds belong to that terminal. No command
needs a display.

## Workflow

1. Start with `vitals snapshot --json`. It samples twice, a second apart, and
   prints CPU (total, per core, frequency, temperature, load), memory, disks,
   filesystems, network, and the ten busiest apps and processes.
   - `--sort memory|read|write` ranks by something else than CPU.
   - `--top N` lists more (0 lists all); `--interval MS` measures rates over a
     longer window, which helps when the load comes and goes.
2. For the full picture of what is running, `vitals apps --json` lists every
   app and service with its totals, its process IDs, and its top processes.
3. Answer with the app first ("Brave is using 3 cores, mostly one tab's
   renderer"), then the process and command line that explains it.

## Reading the numbers

- `cpu_percent` is relative to one core: 200 means two cores fully busy. Compare
  against `cpu.cores` before calling something a problem.
- App totals come from the app's cgroups, so they include child processes that
  have already exited (a build's compilers) and count shared memory once. They
  can therefore differ from the sum of the listed processes.
- App memory leaves out the page cache, which the kernel hands back on demand;
  `memory.available_bytes` is what is really free.
- A `null` rate means the kernel did not let this user read it (another user's
  process). Say so rather than reading it as zero.
- `kind` tells apps the person launched (`app`) from their own background
  services (`service`), system services (`system`), and the kernel.

## Working with the person

- Never end or kill processes on your own. Name what you would stop and why, and
  let them decide; `vitals open` shows the monitor, where `x` ends the selected
  app or process after asking.
- Every command takes `--json`, exits 0 on success and 1 on failure, and writes
  errors to stderr. `vitals help <command>` lists a command's options.
