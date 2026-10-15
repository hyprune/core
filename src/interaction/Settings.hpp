#pragma once
#include "world/World.hpp"
#include <string>
namespace hyprune::interaction {
using Json = world::Json;
// Global settings presets (IPC 0.30 settings.get/set). A preset gives every graphics and
// player setting a default; the user's explicit overrides (graphics.json values that are
// not null, player.json keys) win over it and survive preset switches. Keys are dotted:
// "graphics.<key>", "player.<key>".
//
// Presets: auto (the rendering tier from the GPU), low, medium, high, ultra and usb-c (a
// laptop on USB-C power rendering on its discrete GPU: offload on, even frame pacing).
Json settingsPresets();
bool validPreset(const std::string &id);
// Every governed key with its default under `preset`; `device` (the GL renderer string)
// resolves the auto tier. Rendering keys a tier sets (msaa, bloom, ...) get the tier's value:
// the preset's, or `quality` when the user chose a tier of their own (graphics.quality).
Json presetDefaults(const std::string &preset, const std::string &device, const Json &quality = nullptr);
// The values a preset sets in a section ("graphics" or "player"), without the section prefix.
Json presetSection(const std::string &preset, const std::string &section);
// Migration (once, when settings.json has no preset yet): the preset an existing graphics.json
// implies (its quality; intel reads low; none: auto).
std::string presetFromGraphics(const Json &graphicsOverrides);
// Migration: the overrides of a section that differ from the preset's defaults (an equal
// value was not a choice the preset could lose). Null graphics values stay "inherit".
Json prunedOverrides(const std::string &section, const Json &overrides, const Json &defaults);
// Keys of a section governed by the presets.
Json governedKeys(const std::string &section);
} // namespace hyprune::interaction
