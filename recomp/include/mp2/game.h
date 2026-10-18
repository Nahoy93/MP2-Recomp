#pragma once

// Facts about the game this port is pinned to, and the pieces of the port that
// other files reach for. Every address here belongs to Mario Party 2 (USA),
// SHA-1 166eda1c05670d337e2c3f15a5db528ae1e5d6e3, and comes from the
// decompilation's symbol_addrs.txt / marioparty2.yaml.

#include <cstdint>
#include <filesystem>
#include <string>

#include "recomp.h"
#include <librecomp/game.hpp>

namespace mp2 {

inline constexpr char kProgramName[] = "Mario Party 2: Recompiled";
inline constexpr char8_t kProgramId[] = u8"mp2-recomp";
inline constexpr char kDisplayName[] = "Mario Party 2";
inline constexpr char8_t kGameId[] = u8"marioparty2.n64.us";
inline constexpr char kModGameId[] = "marioparty2";
inline constexpr char kInternalName[] = "MarioParty2";

inline constexpr uint32_t kEntrypoint = 0x80000400;

// Where the decomp's yaml puts every minigame/board overlay, and how far the
// region reaches. Resident code (entry + main + bss) ends exactly at the start.
inline constexpr uint32_t kOverlayBase = 0x80102800;
inline constexpr uint32_t kOverlayWindow = 0x00100000;

// The audio microcode's text in RDRAM (aspMainTextStart).
inline constexpr uint32_t kAspMainText = 0x800BE960;

// Overlay loader the port replaces (decomp src/rom.c).
inline constexpr uint32_t kHuRomDmaCodeRead = 0x8007C4E4;

// Settings, controller profiles, saves: %LOCALAPPDATA%\mp2-recomp on Windows,
// $XDG_DATA_HOME/mp2-recomp (or ~/.local/share/mp2-recomp) on Linux. A file
// named portable.txt next to the executable keeps everything there instead.
std::filesystem::path settings_directory();

// overlays.cpp
void register_overlays();
void on_runtime_init(uint8_t* rdram, recomp_context* ctx);

// platform.cpp
struct PlatformCallbacks {
    recomp::rsp::callbacks_t rsp;
    ultramodern::audio_callbacks_t audio;
    ultramodern::input::callbacks_t input;
    ultramodern::gfx_callbacks_t gfx;
    ultramodern::events::callbacks_t events;
    ultramodern::error_handling::callbacks_t error_handling;
    ultramodern::threads::callbacks_t threads;
};
PlatformCallbacks platform_callbacks();
bool init_audio();
void shutdown_platform();
void set_main_volume(double percent);

}  // namespace mp2
