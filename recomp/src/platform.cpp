// The platform callbacks ultramodern asks the port to supply: the window,
// audio output, controller input and the RSP audio microcode. SDL2 does the
// work; the Windows build uses the copy RT64 vendors.

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <SDL.h>
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <SDL_syswm.h>
#endif

#include "recomp.h"
#include <librecomp/game.hpp>
#include <librecomp/rsp.hpp>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/config.hpp>

#include <recompinput/recompinput.h>
#include <recompinput/input_events.h>
#include <recompinput/input_state.h>
#include <recompinput/players.h>
#include <recompinput/profiles.h>
#include <recompui/recompui.h>

#include "mp2/game.h"
#include "mp2/frontend.h"

// The recompiled audio microcode (RSPRecomp output, C++ linkage).
RspExitReason aspMain(uint8_t* rdram, uint32_t ucode_addr);

namespace {

SDL_Window* g_window = nullptr;

// -------------------------------------------------------------------- input --

constexpr int kMaxPlayers = 4;

void poll_input() {
    // recompinput must be the only thing draining SDL's event queue, and
    // poll_inputs() is what fills its keyboard snapshot.
    recompinput::poll_inputs();
}

bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    *buttons = 0;
    *x = 0.0f;
    *y = 0.0f;
    if (controller_num < 0 || controller_num >= kMaxPlayers) {
        return false;
    }
    if (recompinput::players::is_single_player_mode() && controller_num != 0) {
        return false;
    }
    // While a menu is open the game gets nothing; otherwise the key that closes
    // the menu would also reach the game behind it.
    if (mp2::frontend::menu_has_input()) {
        return true;
    }
    return recompinput::profiles::get_n64_input(controller_num, buttons, x, y);
}

void set_rumble(int controller_num, bool rumble) {
    recompinput::set_rumble(controller_num, rumble);
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    // Single player: only port 1 has a controller, the other characters are
    // played by the computer. With players assigned in the multiplayer menu,
    // each assigned player gets a port.
    bool connected = controller_num == 0;
    if (!recompinput::players::is_single_player_mode() && controller_num > 0 && controller_num < kMaxPlayers) {
        connected = recompinput::players::get_player_is_assigned(controller_num);
    }
    if (connected) {
        return { ultramodern::input::Device::Controller, ultramodern::input::Pak::RumblePak };
    }
    return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
}

// -------------------------------------------------------------------- audio --
//
// The game hands over 16-bit stereo at its own sample rate. An SDL_AudioStream
// resamples to the device rate; the device is fed with SDL_QueueAudio.

SDL_AudioDeviceID g_audio_device = 0;
SDL_AudioStream* g_audio_stream = nullptr;
std::mutex g_audio_mutex;
uint32_t g_input_rate = 32000;
int g_output_rate = 48000;
std::atomic<float> g_volume{ 1.0f };
constexpr int kChannels = 2;

void rebuild_stream_locked() {
    if (g_audio_stream != nullptr) {
        SDL_FreeAudioStream(g_audio_stream);
    }
    g_audio_stream = SDL_NewAudioStream(AUDIO_F32, kChannels, static_cast<int>(g_input_rate),
                                        AUDIO_F32, kChannels, g_output_rate);
    if (g_audio_stream == nullptr) {
        std::fprintf(stderr, "[mp2] SDL_NewAudioStream failed: %s\n", SDL_GetError());
    }
}

void queue_samples(int16_t* audio_data, size_t sample_count) {
    static std::vector<float> converted;
    static std::vector<float> resampled;

    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (g_audio_device == 0 || g_audio_stream == nullptr) {
        return;
    }

    // RDRAM is stored word-swapped, so each stereo pair arrives right-left.
    const float volume = g_volume.load(std::memory_order_relaxed) * (1.0f / 32768.0f);
    converted.resize(sample_count);
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        converted[i + 0] = audio_data[i + 1] * volume;
        converted[i + 1] = audio_data[i + 0] * volume;
    }

    // Keep latency bounded: drop this chunk if a lot is already waiting.
    const uint32_t queued_bytes = SDL_GetQueuedAudioSize(g_audio_device);
    const uint32_t max_bytes = static_cast<uint32_t>(g_output_rate / 5) * kChannels * sizeof(float);  // 200 ms
    if (queued_bytes > max_bytes) {
        return;
    }

    SDL_AudioStreamPut(g_audio_stream, converted.data(), static_cast<int>(converted.size() * sizeof(float)));
    const int available = SDL_AudioStreamAvailable(g_audio_stream);
    if (available > 0) {
        resampled.resize(static_cast<size_t>(available) / sizeof(float));
        const int got = SDL_AudioStreamGet(g_audio_stream, resampled.data(), available);
        if (got > 0) {
            SDL_QueueAudio(g_audio_device, resampled.data(), static_cast<uint32_t>(got));
        }
    }
}

size_t get_frames_remaining() {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (g_audio_device == 0) {
        return 0;
    }
    // Output frames queued, expressed at the game's rate, minus one video frame
    // of headroom so the game keeps a little ahead of the device.
    const uint64_t out_frames = SDL_GetQueuedAudioSize(g_audio_device) / (kChannels * sizeof(float));
    uint64_t in_frames = out_frames * g_input_rate / static_cast<uint64_t>(g_output_rate);
    const uint64_t headroom = g_input_rate / 60;
    in_frames = in_frames > headroom ? in_frames - headroom : 0;
    return static_cast<size_t>(in_frames);
}

void set_frequency(uint32_t frequency) {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (frequency == 0) {
        return;
    }
    g_input_rate = frequency;
    rebuild_stream_locked();
}

// ---------------------------------------------------------------------- rsp --

RspExitReason unknown_ucode(uint8_t*, uint32_t ucode_addr) {
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "[mp2] unknown RSP microcode at 0x%08X; its tasks are skipped\n", ucode_addr);
    }
    return RspExitReason::Broke;
}

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    // Graphics tasks never reach here; ultramodern sends them to RT64.
    const uint32_t ucode = static_cast<uint32_t>(task->t.ucode) & 0x00FFFFFFu;
    if (task->t.type == M_AUDTASK || ucode == (mp2::kAspMainText & 0x00FFFFFFu)) {
        return aspMain;
    }
    std::fprintf(stderr, "[mp2] RSP task type %u, ucode 0x%08X\n",
                 static_cast<unsigned>(task->t.type), static_cast<uint32_t>(task->t.ucode));
    return unknown_ucode;
}

// ---------------------------------------------------------- window and loop --

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "[mp2] SDL video/controller init failed: %s\n", SDL_GetError());
    }
    return nullptr;
}

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    const bool fullscreen = ultramodern::renderer::get_graphics_config().wm_option ==
                            ultramodern::renderer::WindowMode::Fullscreen;

    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(RT64_SDL_WINDOW_VULKAN)
    // RT64 creates its Vulkan surface on this window; SDL refuses unless the
    // window was made as a Vulkan window.
    flags |= SDL_WINDOW_VULKAN;
#endif

    // RT64 expands the picture into the swap chain's shape, so fullscreen opens
    // at the display's own size. Windowed: the largest multiple of 320x240 that
    // fits on the screen.
    int width = 1280;
    int height = 960;
    SDL_Rect bounds{};
    if (fullscreen && SDL_GetDisplayBounds(0, &bounds) == 0) {
        width = bounds.w;
        height = bounds.h;
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    else if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
        int scale = 4;
        while (scale > 1 && (320 * scale > bounds.w || 240 * scale > bounds.h)) {
            --scale;
        }
        width = 320 * scale;
        height = 240 * scale;
    }

    g_window = SDL_CreateWindow(mp2::kProgramName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, flags);
    if (g_window == nullptr) {
        std::fprintf(stderr, "[mp2] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return {};
    }
    SDL_SetWindowMinimumSize(g_window, 320, 240);
    mp2::frontend::publish_window(g_window);

#if defined(_WIN32)
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (SDL_GetWindowWMInfo(g_window, &wm_info) != SDL_TRUE) {
        std::fprintf(stderr, "[mp2] SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return {};
    }
    return ultramodern::renderer::WindowHandle{ wm_info.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__)
    return g_window;
#else
#   error "Unsupported platform"
#endif
}

void update_gfx(ultramodern::gfx_callbacks_t::gfx_data_t) {
    // librecomp's main loop body, on the main thread.
    recompinput::handle_events();
    recompinput::update_rumble();
}

// ------------------------------------------------------------------- misc --

void message_box(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    recompui::message_box(msg);
}

std::string get_game_thread_name(const OSThread* t) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "mp2_%d", t != nullptr ? static_cast<int>(t->id) : -1);
    return std::string(buf);
}

void vi_callback() {}
void gfx_init_callback() {}

}  // namespace

namespace mp2 {

bool init_audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::string error = std::string("Kunde inte starta ljudet / Failed to initialise audio.\nSDL: ") + SDL_GetError();
        recompui::message_box(error.c_str());
        return false;
    }
    SDL_AudioSpec desired{};
    desired.freq = 48000;
    desired.format = AUDIO_F32;
    desired.channels = kChannels;
    desired.samples = 0x100;
    SDL_AudioSpec obtained{};
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (g_audio_device == 0) {
        std::fprintf(stderr, "[mp2] no audio device (%s); continuing without sound\n", SDL_GetError());
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        g_output_rate = obtained.freq;
        rebuild_stream_locked();
    }
    SDL_PauseAudioDevice(g_audio_device, 0);
    return true;
}

void set_main_volume(double percent) {
    g_volume.store(static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0), std::memory_order_relaxed);
}

void shutdown_platform() {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    if (g_audio_device != 0) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    if (g_audio_stream != nullptr) {
        SDL_FreeAudioStream(g_audio_stream);
        g_audio_stream = nullptr;
    }
}

PlatformCallbacks platform_callbacks() {
    PlatformCallbacks cb{};
    cb.rsp.get_rsp_microcode = get_rsp_microcode;
    cb.audio.queue_samples = queue_samples;
    cb.audio.get_frames_remaining = get_frames_remaining;
    cb.audio.set_frequency = set_frequency;
    cb.input.poll_input = poll_input;
    cb.input.get_input = get_input;
    cb.input.set_rumble = set_rumble;
    cb.input.get_connected_device_info = get_connected_device_info;
    cb.gfx.create_gfx = create_gfx;
    cb.gfx.create_window = create_window;
    cb.gfx.update_gfx = update_gfx;
    cb.events.vi_callback = vi_callback;
    cb.events.gfx_init_callback = gfx_init_callback;
    cb.error_handling.message_box = message_box;
    cb.threads.get_game_thread_name = get_game_thread_name;
    return cb;
}

}  // namespace mp2
