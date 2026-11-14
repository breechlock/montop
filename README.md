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
- **Rewind**: point at a spike in the CPU, memory, or disk graph to see which
  apps caused it, and click to see the app list as it was then. `[` and `]`
  step back and forward through the last ten minutes; `Esc` returns to now.
  No more catching a spike only after it's gone.
- **Pause or slow down an app** instead of killing it: `p` freezes every
  process in the app until you press `p` again, and `c` caps how much CPU it
  may use. Your work stays where it was. Both undo themselves when Vitals
  closes.

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
| `Esc` | Return to now, clear the filter, close the shortcuts |
| `s` | Sort by the next column (or click a column header) |
| `r` | Reverse the sort |
| `Shift K` | Show or hide kernel threads |
| `[` `]` / `{` `}` | Rewind or go forward 10 s, or a minute |
| `p` | Pause or resume the selected app |
| `c` | Limit the selected app's CPU |
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

## Rewinding

Every process scan (once a second) is kept for ten minutes: every app or
process that was busy, and the twenty largest of each, which is enough to
explain a spike for about 10 MB. Rewinding shows that list frozen at the
moment picked, while sampling carries on underneath. Ending, pausing, or
limiting is refused until you're back at now, since the PIDs of the past may
belong to other processes by then. The history lives only while the window is
open.

## Pausing and limiting

Pausing freezes the app's cgroup and limiting sets its `CPUQuota`, both through
systemd's user manager (`systemctl --user freeze` and `set-property --runtime`),
so no root is needed and systemd stays in charge. Vitals acts only on your apps
(`app.slice`) and background services (`background.slice`). It refuses the
desktop's own services in `session.slice` (the compositor, D-Bus, audio,
portals), which would freeze the session, system services, which need root,
and the app Vitals itself runs in, such as the terminal you started it from.

## For scripts and agents

```sh
vitals snapshot            # CPU, memory, disks, network, busiest apps and processes
vitals snapshot --json --sort memory --top 20
vitals apps --json         # every app and service, with its PIDs and top processes
vitals pause brave         # freeze an app until: vitals resume brave
vitals limit brave 200     # cap it at two cores until logout; vitals limit brave off
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
