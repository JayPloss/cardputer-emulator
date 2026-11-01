// M5GFX picks a platform by ARDUINO before it looks at SDL. The firmware needs
// ARDUINO (for Print and String), so this sends that branch to the SDL back end.
#pragma once
#define LGFX_PLATFORM_SDL
#include <lgfx/v1/platforms/sdl/common.hpp>
