#pragma once

// The launcher, the settings menu (opened with Escape) and input profiles,
// all provided by RecompFrontend. This file decides what is in them.

#include <SDL.h>
#include <librecomp/game.hpp>
#include <ultramodern/renderer_context.hpp>

namespace mp2::frontend {

// Before recomp::start: names, fonts, keyboard defaults, settings tabs.
void init(const recomp::GameEntry& game);

// After recomp::start returns: writes the key bindings to disk.
void shutdown();

// recompui draws through the window the game uses.
void publish_window(SDL_Window* window);

// RecompFrontend's renderer draws the game and the menus together.
ultramodern::renderer::callbacks_t renderer_callbacks();

// True while a menu has focus; the game then gets no input.
bool menu_has_input();

}  // namespace mp2::frontend
