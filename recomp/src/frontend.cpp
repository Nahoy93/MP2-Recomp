// The launcher, the settings menu and input profiles, from RecompFrontend.
//
// RecompFrontend already provides the menu that Escape opens (its config
// modal), the launcher with the ROM picker, controller and keyboard remapping
// and per-device profiles. What a port supplies is: what the game is called,
// which tabs the menu has, default key bindings and a stylesheet.
//
// Wiring details (globals recompui expects, tab ordering, saving bindings on
// exit) follow Body Harvest: Recompiled (MIT).

#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include <recompui/recompui.h>
#include <recompui/config.h>
#include <recompui/program_config.h>
#include <recompui/renderer.h>
#include <recompinput/input_mapping.h>
#include <recompinput/players.h>
#include <recompinput/profiles.h>

#include <librecomp/config.hpp>
#include <librecomp/game.hpp>
#include <ultramodern/config.hpp>

#include "mp2/game.h"
#include "mp2/frontend.h"

// recompui declares these extern in its own sources and links against the
// port's definitions (ui_launcher.cpp, ui_state.cpp).
std::vector<recomp::GameEntry> supported_games;
SDL_Window* window = nullptr;

namespace {

std::filesystem::path controls_config_path() {
    return recomp::get_config_path() / (recompui::config::controls::id + ".json");
}

std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
        uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    // PresentEarly shows each frame as soon as it is drawn: less latency, and
    // it lets RT64 generate in-between frames for higher frame rates.
    return recompui::renderer::create_render_context(
        rdram, window_handle, ultramodern::renderer::PresentationMode::PresentEarly, developer_mode);
}

void build_launcher(recompui::LauncherMenu* menu) {
    recompui::GameOptionsMenu* options = menu->init_game_options_menu(
        std::u8string{ mp2::kGameId }, mp2::kModGameId, mp2::kDisplayName,
        // No thumbnail: nothing derived from the cartridge is shipped.
        {});

    options->add_start_game_or_load_rom_option("Välj ROM", "Starta spelet");
    options->add_setup_controls_option("Kontroller");
    options->add_settings_option("Inställningar");
    options->add_mods_option("Moddar");
    options->add_exit_option("Avsluta");
}

// Default keyboard layout for player 1. Arrow keys for the stick, the right
// hand on the buttons. Every key can be changed under Kontroller.
void set_keyboard_defaults() {
    using recompinput::GameInput;
    using recompinput::InputField;
    const struct { GameInput input; SDL_Scancode key; SDL_Scancode alt; } keys[] = {
        { GameInput::X_AXIS_NEG, SDL_SCANCODE_LEFT,   SDL_SCANCODE_A },
        { GameInput::X_AXIS_POS, SDL_SCANCODE_RIGHT,  SDL_SCANCODE_D },
        { GameInput::Y_AXIS_POS, SDL_SCANCODE_UP,     SDL_SCANCODE_W },
        { GameInput::Y_AXIS_NEG, SDL_SCANCODE_DOWN,   SDL_SCANCODE_S },
        { GameInput::A,          SDL_SCANCODE_X,      SDL_SCANCODE_SPACE },
        { GameInput::B,          SDL_SCANCODE_Z,      SDL_SCANCODE_LSHIFT },
        { GameInput::Z,          SDL_SCANCODE_C,      SDL_SCANCODE_UNKNOWN },
        { GameInput::START,      SDL_SCANCODE_RETURN, SDL_SCANCODE_UNKNOWN },
        { GameInput::L,          SDL_SCANCODE_Q,      SDL_SCANCODE_UNKNOWN },
        { GameInput::R,          SDL_SCANCODE_E,      SDL_SCANCODE_UNKNOWN },
        { GameInput::C_UP,       SDL_SCANCODE_I,      SDL_SCANCODE_UNKNOWN },
        { GameInput::C_DOWN,     SDL_SCANCODE_K,      SDL_SCANCODE_UNKNOWN },
        { GameInput::C_LEFT,     SDL_SCANCODE_J,      SDL_SCANCODE_UNKNOWN },
        { GameInput::C_RIGHT,    SDL_SCANCODE_L,      SDL_SCANCODE_UNKNOWN },
        { GameInput::DPAD_UP,    SDL_SCANCODE_T,      SDL_SCANCODE_UNKNOWN },
        { GameInput::DPAD_DOWN,  SDL_SCANCODE_G,      SDL_SCANCODE_UNKNOWN },
        { GameInput::DPAD_LEFT,  SDL_SCANCODE_F,      SDL_SCANCODE_UNKNOWN },
        { GameInput::DPAD_RIGHT, SDL_SCANCODE_H,      SDL_SCANCODE_UNKNOWN },
    };
    for (const auto& binding : keys) {
        std::vector<InputField> fields{ InputField::keyboard(binding.key) };
        if (binding.alt != SDL_SCANCODE_UNKNOWN) {
            fields.push_back(InputField::keyboard(binding.alt));
        }
        recompinput::set_default_mapping_for_keyboard(binding.input, fields);
    }
}

}  // namespace

namespace mp2::frontend {

void init(const recomp::GameEntry& game) {
    supported_games.clear();
    supported_games.push_back(game);

    // Must be set before anything is built; the id also names the settings
    // folder registered in main.cpp.
    recompui::programconfig::set_program_name(kProgramName);
    recompui::programconfig::set_program_id(std::u8string{ kProgramId });

    // The family declared inside the font file, which assets/recomp.rcss also
    // names. A mismatch draws every menu with no text in it.
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");

    set_keyboard_defaults();
    recompui::register_launcher_init_callback(build_launcher);

    // Step 1: one human player, keyboard and every pad merged into player 1;
    // the other three characters are played by the computer. Local
    // multiplayer assignment arrives with the controls step.
    recompinput::players::set_player_count_range(1, 4);
    recompinput::players::set_single_player_mode(true);

    // The tabs of the menu Escape opens. create_*_tab may reallocate the list
    // of tabs, so each tab is configured completely before the next is made.
    recompui::config::GeneralTabOptions general{};
    general.has_rumble_strength = true;
    general.has_gyro_sensitivity = false;
    general.has_mouse_sensitivity = false;
    recompui::config::create_general_tab(general, "Allmänt");

    recompui::config::create_controls_tab("Kontroller");

    // Resolution, aspect ratio, HUD placement, frame rate, anti-aliasing,
    // window mode: RT64 options the library already exposes.
    recompui::config::create_graphics_tab("Grafik");

    auto& sound = recompui::config::create_sound_tab("Ljud");
    sound.add_option_change_callback(
        recompui::config::sound::options::main_volume,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&value)) {
                mp2::set_main_volume(*percent);
            }
        });

    recompui::config::create_mods_tab("Moddar");

    const bool first_run = !std::filesystem::exists(recomp::get_config_path() / "graphics.json");

    // Loads saved settings. Must come after every tab.
    recompui::config::finalize();

    // Bindings are otherwise only written when the Controls tab closes.
    if (!std::filesystem::exists(controls_config_path())) {
        recompinput::profiles::save_controls_config(controls_config_path());
    }

    if (first_run) {
        std::printf("[mp2] first run: default settings written to %s\n",
                    recomp::get_config_path().string().c_str());
    }
}

void shutdown() {
    recompinput::profiles::save_controls_config(controls_config_path());
}

void publish_window(SDL_Window* sdl_window) {
    ::window = sdl_window;
}

ultramodern::renderer::callbacks_t renderer_callbacks() {
    ultramodern::renderer::callbacks_t callbacks{};
    callbacks.create_render_context = create_render_context;
    return callbacks;
}

bool menu_has_input() {
    return recompui::is_context_capturing_input();
}

}  // namespace mp2::frontend
