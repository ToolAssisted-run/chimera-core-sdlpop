/* sdlpop-driver.h - Prince of Persia (SDLPoP) as a machine that is stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): the DOS game's
 * keyboard controls. Restart Level is the game's Ctrl+A.
 */
#ifndef SDLPOP_DRIVER_H
#define SDLPOP_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PopButton
{
	POP_BTN_UP,
	POP_BTN_DOWN,
	POP_BTN_LEFT,
	POP_BTN_RIGHT,
	POP_BTN_SHIFT,
	POP_BTN_ENTER,
	POP_BTN_RESTART_LEVEL,
	POP_BTN_COUNT
};

#define POP_VIDEO_WIDTH 320
#define POP_VIDEO_HEIGHT 200
/* The longest step the game takes is a few ticks of its 60 Hz clock; one
 * second of sound is far more than any step produces. A step longer than that
 * still runs the sound for its whole length; only what is handed out is cut. */
#define POP_AUDIO_MAX_SAMPLES 44100
#define POP_AUDIO_RATE 44100

/* The clock: 1,764,000 counts a second = 44100 x 40, so a sample, a
 * millisecond, a tick of the game's 60 Hz timer and a step of its 120 Hz
 * transition are all whole numbers of counts. */
#define POP_CLOCK_HZ 1764000ULL
#define POP_COUNTS_PER_MS 1764ULL
#define POP_COUNTS_PER_UNIT 29400ULL
#define POP_COUNTS_PER_SAMPLE 40ULL

/* 0 on failure, with the reason in err */
int popdrv_init(char *err, int errsize);
void popdrv_set_button(int index, int down);
/* runs the game to the end of its next step */
void popdrv_frame(int render);
const uint32_t *popdrv_video(void);
const int16_t *popdrv_audio(int *samples);
int popdrv_input_was_read(void);
void popdrv_vsync(int *num, int *den);
uint64_t popdrv_clock(void);

/* the property block and the raw domains (game-state.c) */
int popdrv_domain_count(void);
const char *popdrv_domain_name(int i);
uint8_t *popdrv_domain_ptr(int i);
int64_t popdrv_domain_size(int i);
const char *popdrv_game_properties(void);

/* game-state.c */
int gamestate_init(char *err, int errsize);
void gamestate_from_game(void);
void gamestate_to_game(void);

#ifdef __cplusplus
}
#endif

#endif
