// Nested test wallpaper: a background layer-shell surface on every screen that
// flips colour every 2 s, like a wallpaper slideshow (same surface, new commits).
import QtQuick
import Quickshell
import Quickshell.Wayland
ShellRoot {
    Variants {
        model: Quickshell.screens
        PanelWindow {
            id: wallpaper
            required property var modelData
            screen: modelData
            WlrLayershell.layer: WlrLayer.Background
            WlrLayershell.namespace: "hyprune-test-wallpaper"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            exclusionMode: ExclusionMode.Ignore
            anchors { top: true; bottom: true; left: true; right: true }
            property bool red: true
            color: red ? "#d0302a" : "#2060d0"
            Timer { interval: 2000; running: true; repeat: true; onTriggered: wallpaper.red = !wallpaper.red }
        }
    }
}
