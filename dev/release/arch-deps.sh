#!/usr/bin/env bash
# Install core's build dependencies in an Arch container from one Arch Linux
# Archive snapshot, so every release build sees the same Hyprland ABI.
#
#   dev/release/arch-deps.sh YYYY/MM/DD
set -euo pipefail
snapshot=${1:?usage: arch-deps.sh YYYY/MM/DD}
echo "Server = https://archive.archlinux.org/repos/$snapshot/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist
pacman -Syyuu --noconfirm --needed base-devel cmake ninja git pkgconf hyprland hyprgraphics pixman libdrm \
    libxkbcommon pango wayland wayland-protocols mesa libxml2 openssl lua libpng freetype2 fontconfig \
    vulkan-icd-loader vulkan-headers python github-cli
pacman -Q hyprland aquamarine hyprutils hyprgraphics hyprcursor hyprlang
