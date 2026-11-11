#pragma once
// Forced into each firmware file, because M5Canvas has to be the screen-reading
// canvas before the firmware's first use of it.
#include <M5Cardputer.h>

// The card's POSIX directory calls: the device mounts the card at /sd. Paths under it go to the run's card,
// each open directory costs what ESP-IDF's FAT driver allocates for one, and readdir allocates nothing.
#include <dirent.h>
extern "C" DIR *emu_opendir(const char *path);
extern "C" struct dirent *emu_readdir(DIR *dir);
extern "C" int emu_closedir(DIR *dir);
#define opendir emu_opendir
#define readdir emu_readdir
#define closedir emu_closedir
