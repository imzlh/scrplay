#ifndef SC_ICON_H
#define SC_ICON_H

#include "common.h"

#include <SDL2/SDL_surface.h>

SDL_Surface *
scrplay_icon_load(void);

void
scrplay_icon_destroy(SDL_Surface *icon);

#endif
