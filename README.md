# Vitals

See what your system is doing: CPU, memory, disks, network, and processes, in
one window that looks like the rest of your Omarchy desktop. Inspired by htop,
bpytop, and btop.

- **CPU**: total load history, per-core gauges, frequency, package temperature, load average.
- **Memory**: used, cached, available, and swap, with history.
- **Disks**: read and write throughput per physical disk (reads above the line,
  writes below), how busy each one is, and how full each filesystem is.
- **Network**: download and upload per connected interface, with totals.
- **Apps**: processes grouped into the apps and services they belong to, so a
  browser's forty helpers are one row, and a terminal's shells and builds belong
  to that terminal. Open an app to see its processes, or press `a` for the
  classic flat process list. Sort, filter, and end or kill an app or a process
  from the keyboard.

Vitals follows the current Omarchy theme live, the whole palette, light or dark,
along with Hyprland's window rounding, the desktop's monospace font, and the
language picked under Setup > Language.

## Shortcuts

| Key | Action |
| --- | --- |
| `?` | Show or hide shortcuts |
| `Space` | Pause or resume updates |
| `+` / `-` | Sample less or more often (0.125 s to 4 s; 0.25 s by default) |
| `1` – `5` | Show or hide CPU, memory, disks, network, processes |
| `a` | Show apps, or every process |
| `↑` `↓` / `j` `k` | Select an app or process |
| `→` `←` / `l` `h` / `Enter` | Open or close an app |
| `PgUp` `PgDn` | Move a page |
| `g` / `G` | First or last process |
| `/` | Filter processes by name, command, user, or PID |
| `Esc` | Clear the filter, close the shortcuts |
| `s` | Sort by the next column (or click a column header) |
| `r` | Reverse the sort |
| `Shift K` | Show or hide kernel threads |
| `x` | End the selected app or process (SIGTERM), after asking |
| `Shift X` | Kill the selected app or process (SIGKILL), after asking |
| `q` | Quit |

Graphs are drawn by the GPU through Qt Quick's scene graph and scroll
continuously between samples at a fixed 2 pixels per sample, so a wider window
shows more history. A frame is drawn only when something moves by a whole
pixel, which keeps Vitals at about 4 % of one core while it runs. The process
list refreshes once a second (or every two samples, if slower), since a table
rewritten faster than that can't be read.

## How apps are found

systemd runs every app Omarchy launches in its own cgroup, named after the app
(`app-Hyprland-brave-1a2b.scope`), and every service in one named after the
service. Vitals groups processes by those cgroups, names each group from its
desktop entry, and takes the group's totals from the cgroup's own counters.
Those are exact where adding up processes is not: they include child processes
that already exited (a build's compilers), count shared memory once, and leave
out the page cache. Processes without a cgroup Vitals can read stand alone.

## For scripts and agents

```sh
vitals snapshot            # CPU, memory, disks, network, busiest apps and processes
vitals snapshot --json --sort memory --top 20
vitals apps --json         # every app and service, with its PIDs and top processes
vitals skill install       # teach Claude Code and Codex to use Vitals
```

Commands need no display. Each takes `--json`, exits 0 or 1, and writes errors
to stderr; `vitals help <command>` lists its options. Bare `vitals` prints this
help; `vitals open` opens the window, as the launcher entry does.

CPU% is measured against one core, as in top and htop, so a process busy on two
cores shows 200. Per-process disk IO shows a dash for processes owned by other
users: the kernel only lets their owner read those counters.

## Build

```sh
bin/build      # build/vitals
bin/test       # Qt Test, offscreen
bin/install    # makepkg -fsi: installs the binary, icon, and launcher entry
```

Needs `qmake6`, `make`, a C++17 compiler, and Qt 6 (base, declarative, wayland),
all part of Omarchy.

## License

MIT. Theme and language following adapted from Omacut and Monologue.
