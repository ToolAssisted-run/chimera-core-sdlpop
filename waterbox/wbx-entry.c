/* wbx-entry.c - the chimera guest ABI over sdlpop-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h), which is what makes the equivalence gate
 * a real proof: the same driver, the same exports, one in the sandbox and one
 * out of it.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "sdlpop-driver.h"

static char g_load_error[1024];

/* A controller of 64 buttons or fewer arrives as a packed mask in FrameAdvance;
 * the gate harness drives SetButton. A step sees their union. */
static uint8_t g_set_buttons[POP_BTN_COUNT];

/* Turbo: the picture is not converted. Only the conversion - the game draws
 * its screen whatever happens, because what it draws is part of the game. */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return popdrv_init(g_load_error, (int)sizeof g_load_error);
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < POP_BTN_COUNT) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < POP_BTN_COUNT; i++)
		popdrv_set_button(i, g_set_buttons[i] | (int)((packed >> i) & 1));
	popdrv_frame(chimera_render_enabled);
}

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return (uint32_t *)popdrv_video(); }
ECL_EXPORT int GetVideoWidth(void) { return POP_VIDEO_WIDTH; }
ECL_EXPORT int GetVideoHeight(void) { return POP_VIDEO_HEIGHT; }

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)popdrv_audio(&n);
}

ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	popdrv_audio(&n);
	return n;
}

/* the length of the step just run, as a rate: 12/1 for a tick of play */
ECL_EXPORT int GetVsyncNumerator(void)
{
	int num, den;
	popdrv_vsync(&num, &den);
	return num;
}

ECL_EXPORT int GetVsyncDenominator(void)
{
	int num, den;
	popdrv_vsync(&num, &den);
	return den;
}

ECL_EXPORT int InputWasRead(void) { return popdrv_input_was_read(); }

/* memory domains: Game State (the property block), then the level, the moving
 * objects and the tile animations in place */
ECL_EXPORT int GetMemoryDomainCount(void) { return popdrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return popdrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return popdrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return popdrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && i < popdrv_domain_count() ? 1 : 0; }

/* docs/game-cores.md: the property table */
ECL_EXPORT const char *GetGameProperties(void) { return popdrv_game_properties(); }

/* the machine's own clock (1,764,000 counts a second), for harnesses that
 * compare machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return popdrv_clock(); }
