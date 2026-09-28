/* SDL_image.h - the part of SDL_image's interface SDLPoP names, and no more.
 *
 * SDLPoP loads PNG only from SDLPoP's own extracted data folders, its window
 * icon and the lighting mask; the core uses none of them (the game's pictures
 * come from the original DAT files, decoded by SDLPoP itself). So there is no
 * image library in the core: these are declared here and answered "no" by
 * seams.c, and a PNG can never stand in for a missing DAT.
 */
#ifndef CHIMERA_SDL_IMAGE_STUB_H
#define CHIMERA_SDL_IMAGE_STUB_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
int IMG_SavePNG(SDL_Surface *surface, const char *file);
#define IMG_GetError SDL_GetError

#ifdef __cplusplus
}
#endif

#endif
