# Vitals

See what your system is doing: CPU, memory, disks, network, and processes, in
one window that looks like the rest of your Omarchy desktop. Inspired by htop,
bpytop, and btop.

- **CPU**: total load history, per-core gauges, frequency, package temperature, load average.
- **Memory**: used, cached, available, and swap, with history.
- **Disks**: read and write throughput per physical disk (reads above the line,
  writes below), how busy each one is, and how full each filesystem is.
- **Network**: download and upload per connected interface, with totals.
- **Processes**: sortable and filterable, with CPU, memory, threads, and per-process
  disk reads and writes. End or kill the selected one from the keyboard.

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
| `↑` `↓` / `j` `k` | Select a process |
| `PgUp` `PgDn` | Move a page |
| `g` / `G` | First or last process |
| `/` | Filter processes by name, command, user, or PID |
| `Esc` | Clear the filter, close the shortcuts |
| `s` | Sort by the next column (or click a column header) |
| `r` | Reverse the sort |
| `Shift K` | Show or hide kernel threads |
| `x` | End the selected process (SIGTERM), after asking |
| `Shift X` | Kill the selected process (SIGKILL), after asking |
| `q` | Quit |

Graphs are drawn by the GPU through Qt Quick's scene graph and scroll
continuously between samples at a fixed 2 pixels per sample, so a wider window
shows more history. A frame is drawn only when something moves by a whole
pixel, which keeps Vitals at about 4 % of one core while it runs. The process
list refreshes once a second (or every two samples, if slower), since a table
rewritten faster than that can't be read.

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
