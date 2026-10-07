hl.monitor({ output = "", mode = "1600x900", position = "0x0", scale = 1 })
hl.monitor({ output = "HEADLESS-1", mode = "1920x1080@60", position = "1600x0", scale = 1 })
hl.config({
    general = { gaps_in = 5, gaps_out = 10, border_size = 2 },
    decoration = { rounding = 4 },
    animations = { enabled = false },
    cursor = { no_hardware_cursors = true },
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true, disable_watchdog_warning = true },
})
hl.bind("SUPER + RETURN", hl.dsp.exec_cmd("kitty"))
hl.bind("SUPER + Q", hl.dsp.window.close())
if hl.plugin.hyprune then
    hl.bind("SUPER + F12", hl.plugin.hyprune.toggle)
end
