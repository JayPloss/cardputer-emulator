// The interactive window, for a person: the screen at 3x, the PC keyboard as the Cardputer's.
// Arrow keys are the arrow cluster, Esc is the top-left key, F1 is the rear button,
// Left Ctrl is Fn, and letters, digits and Enter are themselves.
#include <SDL2/SDL.h>

#include <map>

#include "emu.h"

namespace emu {
namespace {
struct Key { int x, y; };
bool keyFor(SDL_Keycode code, Key &key) {
  static const std::map<SDL_Keycode, Key> special = {
      {SDLK_ESCAPE, {0, 0}}, {SDLK_BACKSPACE, {13, 0}}, {SDLK_TAB, {0, 1}}, {SDLK_LCTRL, {0, 2}}, {SDLK_LSHIFT, {1, 2}}, {SDLK_RETURN, {13, 2}},
      {SDLK_LALT, {2, 3}}, {SDLK_SPACE, {13, 3}}, {SDLK_UP, {11, 2}}, {SDLK_LEFT, {10, 3}}, {SDLK_DOWN, {11, 3}}, {SDLK_RIGHT, {12, 3}},
  };
  const auto found = special.find(code);
  if (found != special.end()) { key = found->second; return true; }
  static const char *const rows[4] = {"`1234567890-=", "\tqwertyuiop[]\\", "  asdfghjkl;'", "   zxcvbnm,./"};
  for (int y = 0; y < 4; y++) for (int x = 0; rows[y][x]; x++) if (rows[y][x] == code && code != ' ') { key = {x, y}; return true; }
  return false;
}
}  // namespace

void runWindow() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow("Cardputer emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 405, 0);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 240, 135);
  static uint16_t swapped[240 * 135];
  for (;;) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) { SDL_Quit(); return; }
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.key.keysym.sym == SDLK_F1 && event.type == SDL_KEYDOWN) { buttonPress(); continue; }
        Key key;
        if (!event.key.repeat && keyFor(event.key.keysym.sym, key)) keySet(key.x, key.y, event.type == SDL_KEYDOWN);
      }
    }
    const uint16_t *pixels = screenPixels();
    for (int i = 0; i < 240 * 135; i++) swapped[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    SDL_UpdateTexture(texture, nullptr, swapped, 480);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_Delay(30);
  }
}

}  // namespace emu
