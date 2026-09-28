/* seams.c - everything SDLPoP reaches outside itself, answered by the core.
 *
 * Linked with -Wl,--wrap=<name> for each __wrap_ below, in both builds: a call
 * to SDL_Delay from SDLPoP (or from SDL itself) lands in __wrap_SDL_Delay, and
 * SDL and the C library are otherwise used as they are. SDL is the real SDL 2,
 * built from source with its own minimal configuration (the dummy video driver,
 * no threads, no timers, no CPU feature dispatch) - see native.mk.
 *
 * What is replaced, and why:
 *   - time: the performance counter, the tick count and SDL_Delay are the
 *     core's clock (sdlpop-driver.c); SDL_Delay is where a step ends.
 *   - SDL_Init: SDLPoP asks for timers and game controllers, which a build
 *     without threads cannot give; it gets video and events, which is all a
 *     keyboard-driven game with a pulled audio device needs.
 *   - the audio device: SDL_OpenAudio records the callback; the driver pulls it.
 *   - presenting: SDL_UpdateTexture hands the driver the screen; drawing it to
 *     a window is skipped (there is none).
 *   - files: the game sees the data files the project mounted (LEVELS.DAT
 *     being the project's own level set when it brings one), and the
 *     SDLPoP.ini the core writes from the settings, and nothing else. It writes
 *     nothing (saves, the hall of fame, screenshots and replays all fail
 *     quietly, as they would on a read-only disk). So it cannot load SDLPoP's
 *     extracted folders, a mod, a PRINCE.EXE's CusPoP changes or a user's own
 *     SDLPoP.ini, in the sandbox or out of it - which is also what makes the
 *     native reference see what the sandbox sees.
 *   - SDL_PollEvent: counts the pumps that find nothing, for the driver's
 *     safety net under the wait hooks (sdlpop-driver.c, popdrv_idle_pump).
 *   - exit: the game ending (Ctrl+Q, an error) parks it instead of the process.
 *   - SDL_image: not built. SDLPoP only uses it for PNG folders, its icon and
 *     the lighting mask, all of which the core does without.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "sdlpop-driver.h"

/* sdlpop-driver.c */
const char *popdrv_file_for(const char *name);
const char *popdrv_ini(int *len);
void popdrv_delay(uint32_t ms);
void popdrv_audio_open(SDL_AudioCallback cb, void *user);
void popdrv_audio_close(void);
void popdrv_audio_pause(int paused);
int popdrv_audio_status(void);
void popdrv_present(const void *pixels, int pitch);
void popdrv_game_exit(int code) __attribute__((noreturn));
int popdrv_on_game_stack(void);

/* ----------------------------------------------------------------- time */

Uint64 __wrap_SDL_GetPerformanceCounter(void) { return popdrv_clock(); }
Uint64 __wrap_SDL_GetPerformanceFrequency(void) { return POP_CLOCK_HZ; }
Uint32 __wrap_SDL_GetTicks(void) { return (Uint32)(popdrv_clock() / POP_COUNTS_PER_MS); }
Uint64 __wrap_SDL_GetTicks64(void) { return popdrv_clock() / POP_COUNTS_PER_MS; }

void __wrap_SDL_Delay(Uint32 ms)
{
	/* only the game waits; nothing else should, and a wait off the game's
	 * stack would have nowhere to return to */
	if (popdrv_on_game_stack())
		popdrv_delay(ms);
}

/* SDLPoP's Shift+L hands a callback to a timer thread; there are no threads.
 * Unreachable from the panel (no L key), and SDLPoP reports the failure. */
SDL_TimerID __wrap_SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *param)
{
	(void)interval; (void)cb; (void)param;
	return 0;
}
SDL_bool __wrap_SDL_RemoveTimer(SDL_TimerID id) { (void)id; return SDL_FALSE; }

time_t __wrap_time(time_t *t)
{
	/* the machine has no date; SDLPoP only asks for one to seed its random
	 * numbers, which the core seeds from a setting instead */
	if (t) *t = 0;
	return 0;
}

/* ------------------------------------------------------------------- init */

int __real_SDL_Init(Uint32 flags);
int __real_SDL_InitSubSystem(Uint32 flags);
static const Uint32 k_unavailable = SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
	SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR;

int __wrap_SDL_Init(Uint32 flags)
{
	/* SDL's dummy video driver has to be asked for by name; over whatever the
	 * environment says, so a host's SDL_VIDEODRIVER never reaches the native
	 * reference either */
	SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "dummy", SDL_HINT_OVERRIDE);
	return __real_SDL_Init(flags & ~k_unavailable);
}

int __wrap_SDL_InitSubSystem(Uint32 flags)
{
	if (flags & k_unavailable) return SDL_SetError("not in this machine");
	return __real_SDL_InitSubSystem(flags);
}

/* ------------------------------------------------------------------ audio */

int __wrap_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	/* SDLPoP asks for 44100 Hz signed 16-bit stereo, which is exactly what the
	 * core hands out; anything else is a different SDLPoP */
	if (!desired || desired->freq != POP_AUDIO_RATE || desired->format != AUDIO_S16SYS || desired->channels != 2 || !desired->callback)
		return SDL_SetError("the core's audio device is 44100 Hz S16 stereo");
	if (obtained) *obtained = *desired;
	desired->silence = 0;
	popdrv_audio_open(desired->callback, desired->userdata);
	return 0;
}

void __wrap_SDL_CloseAudio(void) { popdrv_audio_close(); }
void __wrap_SDL_PauseAudio(int pause_on) { popdrv_audio_pause(pause_on); }
void __wrap_SDL_LockAudio(void) {}
void __wrap_SDL_UnlockAudio(void) {}
SDL_AudioStatus __wrap_SDL_GetAudioStatus(void) { return (SDL_AudioStatus)popdrv_audio_status(); }

/* ------------------------------------------------------------------ events */

int __real_SDL_PollEvent(SDL_Event *event);
int popdrv_idle_pump(void);

int __wrap_SDL_PollEvent(SDL_Event *event)
{
	int got = __real_SDL_PollEvent(event);
	if (!got && popdrv_idle_pump())
		got = __real_SDL_PollEvent(event);
	return got;
}

/* -------------------------------------------------------------- the screen */

int __wrap_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
	int w = 0, h = 0;
	Uint32 format = 0;
	if (texture && pixels && !rect && SDL_QueryTexture(texture, &format, NULL, &w, &h) == 0 &&
	    w == POP_VIDEO_WIDTH && h == POP_VIDEO_HEIGHT && format == SDL_PIXELFORMAT_RGB24)
		popdrv_present(pixels, pitch);
	return 0;
}

int __wrap_SDL_RenderClear(SDL_Renderer *renderer) { (void)renderer; return 0; }
int __wrap_SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst)
{
	(void)renderer; (void)texture; (void)src; (void)dst;
	return 0;
}
void __wrap_SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }

/* ------------------------------------------------------------------ files */

FILE *__real_fopen(const char *path, const char *mode);
int __real_access(const char *path, int mode);
int __real_stat(const char *path, struct stat *st);

static int is_ini(const char *path) { return path && !strcmp(path, "SDLPoP.ini"); }

/* the core's own channels (read by the driver through the same fopen) */
static int is_channel(const char *path) { return path && (!strcmp(path, "settings") || !strcmp(path, "slots")); }

FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (!path || !mode) { errno = EINVAL; return NULL; }
	if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')) { errno = EROFS; return NULL; }
	if (is_ini(path))
	{
		int len = 0;
		const char *text = popdrv_ini(&len);
		return fmemopen((void *)text, (size_t)len, "r");
	}
	if (is_channel(path))
		return __real_fopen(path, mode);
	const char *mounted = popdrv_file_for(path);
	if (mounted)
		return __real_fopen(mounted, mode);
	errno = ENOENT;
	return NULL;
}

int __wrap_access(const char *path, int mode)
{
	if (mode & W_OK) { errno = EROFS; return -1; }
	if (is_ini(path) || popdrv_file_for(path)) return 0;
	errno = ENOENT;
	return -1;
}

int __wrap_stat(const char *path, struct stat *st)
{
	const char *mounted = popdrv_file_for(path);
	if (mounted) return __real_stat(mounted, st);
	errno = ENOENT;
	return -1;
}

DIR *__wrap_opendir(const char *path)
{
	(void)path;
	errno = ENOENT;
	return NULL;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
	(void)path; (void)mode;
	errno = EROFS;
	return -1;
}

/* ------------------------------------------------------------------- exit */

void __real_exit(int code) __attribute__((noreturn));
void __wrap_exit(int code)
{
	if (popdrv_on_game_stack())
		popdrv_game_exit(code);
	__real_exit(code);
}

/* ------------------------------------------------------------- SDL_image */

SDL_Surface *IMG_Load(const char *file) { (void)file; SDL_SetError("no image loader in this core"); return NULL; }
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc)
{
	if (src && freesrc) SDL_RWclose(src);
	SDL_SetError("no image loader in this core");
	return NULL;
}
int IMG_SavePNG(SDL_Surface *surface, const char *file) { (void)surface; (void)file; return SDL_SetError("no image writer in this core"); }
