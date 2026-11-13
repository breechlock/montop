import QtQuick
import QtQuick.Controls
import Vitals

// A Sparkline reading from the monitor and wired to the window's frame
// clock, so every graph scrolls in step with the others.
Sparkline {
    source: monitor
    step: 2
    phase: ApplicationWindow.window ? ApplicationWindow.window.phase : 1
    gridColor: theme.colors.selection
}
