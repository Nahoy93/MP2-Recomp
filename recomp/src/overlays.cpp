// Section registration and the overlay loader.
//
// Mario Party 2 keeps its resident code in two sections, `entry` (ROM 0x1000)
// and `main` (ROM 0x1060), linked at 0x80000400, followed by main's bss up to
// 0x80102800. Every minigame, board and menu is an overlay linked at that one
// address, 0x80102800, and swapped in by OvlLoad() through
// HuRomDmaCodeRead(rom, ram, size) (decomp src/util.c, src/rom.c).
//
// The runtime has to know which overlay currently occupies 0x80102800 so that
// function pointers into it resolve to the right recompiled code. The game's
// loader copies in 0x4000-byte pieces, and librecomp's load_overlays() only
// recognises a section when one copy covers all of it, so the loader is
// replaced here (config/mp2.us.toml lists it as ignored): one copy, then the
// runtime is told what arrived.
//
// This is the one translation unit that includes the generated section tables.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "recomp.h"
#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>
#include <librecomp/sections.h>

#include "recomp_overlays.inl"

#include "mp2/game.h"

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);

namespace {

std::mutex g_overlay_mutex;

bool env_set(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

const SectionTableEntry* section_at_rom(uint32_t rom) {
    for (size_t i = 0; i < ARRLEN(section_table); ++i) {
        if (section_table[i].rom_addr == rom) {
            return &section_table[i];
        }
    }
    return nullptr;
}

}  // namespace

// The replacement for the game's HuRomDmaCodeRead(u8* src, u8* dest, s32 size).
// Same contract: copy `size` bytes of cartridge ROM at offset `src` to `dest`,
// return 0. No PI messages are posted: the original waited for each of its own
// DMAs before returning, so nothing else is listening.
extern "C" void HuRomDmaCodeRead(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t src = static_cast<uint32_t>(ctx->r4) & 0x0FFFFFFFu;
    const int32_t dest = static_cast<int32_t>(ctx->r5);
    const int32_t size = static_cast<int32_t>(ctx->r6);

    if (size > 0) {
        std::lock_guard<std::mutex> lock(g_overlay_mutex);

        // Forget whatever overlay was in the window. Only ever above the
        // resident sections, so this can never touch `main`.
        if (static_cast<uint32_t>(dest) >= mp2::kOverlayBase) {
            unload_overlays(static_cast<int32_t>(mp2::kOverlayBase), mp2::kOverlayWindow);
        }

        recomp::do_rom_read(rdram, dest, 0x10000000u | src, static_cast<size_t>(size));
        load_overlays(src, dest, static_cast<uint32_t>(size));

        static const bool trace = env_set("MP2_DEBUG_LOADS");
        if (trace) {
            const SectionTableEntry* section = section_at_rom(src);
            std::fprintf(stderr, "[mp2-load] rom 0x%06X -> 0x%08X (0x%X bytes)%s\n",
                         src, static_cast<uint32_t>(dest), static_cast<uint32_t>(size),
                         section != nullptr ? "" : " -- no code section starts here");
        }
    }

    ctx->r2 = 0;
}

namespace mp2 {

void register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections{};
    sections.code_sections = section_table;
    sections.num_code_sections = ARRLEN(section_table);
    sections.total_num_sections = num_sections;

    recomp::overlays::overlays_by_index_t overlays{};
    overlays.table = overlay_sections_by_index;
    overlays.len = ARRLEN(overlay_sections_by_index);

    recomp::overlays::register_overlays(sections, overlays);
}

void on_runtime_init(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;

    // librecomp's boot step registers every section whose ROM lies in the first
    // megabyte after 0x1000, at rom - 0x1000 + 0x80000400. That is right for
    // `entry` and `main`, but the first few overlays (ROM 0xD57F0 onwards) also
    // fall inside it and get registered at bogus addresses in main's bss, some
    // straddling 0x80102800. Clear that range so the first real overlay load
    // starts from nothing.
    unload_overlays(static_cast<int32_t>(0x800C0000u), 0x80200000u - 0x800C0000u);

    // The native loader, for any caller that reaches it through a pointer.
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kHuRomDmaCodeRead), HuRomDmaCodeRead);

    std::printf("[mp2] runtime initialised; %zu code sections registered\n", ARRLEN(section_table));
}

}  // namespace mp2
