/* sdlpop-driver.c - SDLPoP, stepped one game step at a time.
 *
 * THE SHAPE. SDLPoP's main loop is the DOS game's: it runs the machine and
 * waits for its timers inside that loop. So the game runs on a stack of its
 * own (coroutine.c), and every place it would have waited is where it hands
 * control back: FrameAdvance switches in, the game runs until its next wait,
 * and switches out. Nothing is threaded.
 *
 * THE CLOCK is the only time the game sees. SDL_GetPerformanceCounter reads it
 * and SDL_Delay is where it moves (seams.c wraps both); nothing reads the host.
 * It counts 1,764,000 a second, so a sample, a millisecond and the game's 60 Hz
 * and 120 Hz steps are all whole numbers of counts. A step lasts:
 *   - to the deadline of the game timer it waits for, when do_simple_wait is
 *     the one waiting (patch 0001's chimera_wait_for_timer): that is a tick of
 *     play, 12 a second walking and 10 fighting, and it does not look at the
 *     controls while it waits, so the whole wait is one step;
 *   - otherwise to the next tick of the game's 60 Hz clock at the earliest,
 *     and at least as long as the delay the game asked for. Every other wait in
 *     the game polls the controls while it waits (the title, the cutscenes, the
 *     pause), so each 1/60 s of it is a step that reads them. The 120 Hz
 *     screen transition runs two of its steps in each of those, through its own
 *     catch-up logic.
 * The step's length is what GetVsync reports afterwards.
 *
 * INPUT arrives the way SDL would deliver it: FrameAdvance turns the buttons
 * that changed into SDL key events, and the game's own process_events() takes
 * them the moment its wait is over - so the key handling, the "any key" checks
 * and Ctrl+A are upstream's.
 *
 * SOUND is pulled, not pushed: at the end of each step the audio callback SDLPoP
 * registered is run for exactly the samples the step covers, in 1024-sample
 * blocks as SDL's device would ask for them. The game waits for sounds to end
 * (a level's closing music, the title), so this is part of the machine, not
 * only its output.
 *
 * THE PICTURE is what SDLPoP hands SDL_UpdateTexture - its 320x200 screen as it
 * presents it - copied when it is presented.
 *
 * THE DATA are the user's own Prince of Persia 1.0 files, mounted by name as the
 * project's firmware. Init checks each one the game will open by SHA-1 and
 * refuses a missing one or one of another version by name; the game can open
 * nothing else (seams.c), so it cannot fall back to SDLPoP's extracted folders.
 */
#include "common.h"

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "coroutine.h"
#include "sdlpop-driver.h"
#include "sha1.h"

/* seg009.c's timer state */
extern Uint64 timer_last_counter[];
extern int wait_time[];

FILE *__real_fopen(const char *path, const char *mode);
void __real_exit(int code) __attribute__((noreturn));

/* ------------------------------------------------------------ the data files */

enum { NEED_ALWAYS, NEED_DIGITAL, NEED_ORIGINAL_LEVELS };

typedef struct
{
	const char *name;
	long size;
	const char *sha1;
	int need;
} pop_file;

/* Prince of Persia 1.0 (DOS), file by file. The digitized sounds and the AdLib
 * music are needed only with the Sound Blaster; the PC speaker's are always
 * opened, as SDLPoP opens them whatever the sound card. The levels are the
 * original ones unless the project brings a level set of its own. */
static const pop_file k_files[] = {
	{ "PRINCE.DAT", 25600, "57FEDE2FBF1BB4A211066393353B6A2E0060B9BD", NEED_ALWAYS },
	{ "KID.DAT", 37149, "C7E841443C247BF65FF94C13A4B01FDACA767DCA", NEED_ALWAYS },
	{ "LEVELS.DAT", 37031, "2CD58BB1EECE062EF98FD8411377B58F3C72C031", NEED_ORIGINAL_LEVELS },
	{ "TITLE.DAT", 36799, "8C6E587317B34F6A7E79798DF38731B693138B42", NEED_ALWAYS },
	{ "PV.DAT", 25829, "DA20D2C8ED3DA6E93CC882E7023BD502E0FC7BF9", NEED_ALWAYS },
	{ "VDUNGEON.DAT", 10933, "9F71FA4D364175035FB07B4396AAE21E2CFDF41B", NEED_ALWAYS },
	{ "VPALACE.DAT", 14212, "09F261C0493C90D8FA16123C5215A881E138FA58", NEED_ALWAYS },
	{ "GUARD.DAT", 6950, "900C3B2EB4D84DC45E6FD086AE15B93347694103", NEED_ALWAYS },
	{ "GUARD1.DAT", 117, "1F3BD8B571D7E42F230D6D4740B6EC6A81400AF0", NEED_ALWAYS },
	{ "GUARD2.DAT", 117, "32540281AF1594610D2CB83D41F1B5691E89B50D", NEED_ALWAYS },
	{ "FAT.DAT", 6521, "CABACC01E292A54BAE75A7C3514B10231A575454", NEED_ALWAYS },
	{ "SKEL.DAT", 3868, "B02DDFF312ED99EF529D3651FFFD475E6F27C540", NEED_ALWAYS },
	{ "VIZIER.DAT", 6111, "F3BBE710982BBCDB51F43AB5C97D0E23EFD0F633", NEED_ALWAYS },
	{ "SHADOW.DAT", 4715, "CBC976A65730CA2B8F14A7CF79CA14F413701053", NEED_ALWAYS },
	{ "IBM_SND1.DAT", 3684, "92F68AFE878FA233D44BB0338DE374C2C37264B9", NEED_ALWAYS },
	{ "IBM_SND2.DAT", 3784, "CF74CAE4A55521A41E618C1A21A0C6AD92B0412E", NEED_ALWAYS },
	{ "DIGISND1.DAT", 50101, "27DC74B67E3EF9E261762B9D36DC3E01F9C318F5", NEED_DIGITAL },
	{ "DIGISND2.DAT", 32426, "CB5B90C548233F7D48F85769AA8F96D55A2CE087", NEED_DIGITAL },
	{ "DIGISND3.DAT", 31008, "41CB8EAF953E1D54BD9A57727FB3E9CD6B029492", NEED_DIGITAL },
	{ "MIDISND1.DAT", 7096, "30BBCC0C3E893DBA2E4288F600294CC5BA1FC11A", NEED_DIGITAL },
	{ "MIDISND2.DAT", 18958, "8320C09113C4E3B31F00959402F79F31738B09E3", NEED_DIGITAL },
};
#define POP_FILE_COUNT ((int)(sizeof k_files / sizeof k_files[0]))

/* Files known to be something else under the same name: SDLPoP's repository
 * ships these sound files, which are a later release's (1.3-format waves). */
static const struct { const char *sha1; const char *what; } k_other[] = {
	{ "9E1464DAC4078B1754DB25E0DC3A9CD245DE8FE4", "a later release's (as SDLPoP ships it)" },
	{ "62E225351D23338419750AFE82BA8506529521B4", "a later release's (as SDLPoP ships it)" },
	{ "5708C6171F67994993BD210ED28101F752591D22", "a later release's (as SDLPoP ships it)" },
	{ "03FDED77B3A2DDD6487041159796CB835F4D945B", "a later release's (as SDLPoP ships it)" },
	{ "DB1A145AB96C24156944B43DFC229F5424C65856", "a later release's (as SDLPoP ships it)" },
};

/* the project's own level set, if its "levels" slot holds one: the name it is
 * mounted under (sdlpop-driver keeps it in g, below) */
static const char *custom_levels(void);

/* seams.c asks this: which mounted file does the game get when it opens
 * `name`? Only the data files exist for it - with LEVELS.DAT being the
 * project's level set when it has one. NULL: nothing by that name. */
const char *popdrv_file_for(const char *name)
{
	if (custom_levels() && !strcmp(name, "LEVELS.DAT"))
		return custom_levels();
	for (int i = 0; i < POP_FILE_COUNT; i++)
		if (!strcmp(name, k_files[i].name)) return name;
	return NULL;
}

/* ---------------------------------------------------------------- the state */

typedef struct
{
	/* the clock */
	uint64_t now;
	uint64_t pending_until;   /* the deadline do_simple_wait is waiting for, or 0 */
	uint64_t step_start;
	uint64_t step_length;     /* the last completed step */

	int input_read;
	int idle_pumps;           /* event pumps that found nothing since the clock last moved */
	int spin_reported;
	int over, exit_code;      /* the game called exit() */
	int started;

	/* the audio device SDLPoP opened */
	SDL_AudioCallback callback;
	void *callback_user;
	int audio_open, audio_paused;
	int block_pos;
	int16_t block[1024 * 2];
	int16_t out[POP_AUDIO_MAX_SAMPLES * 2];
	int out_samples;

	/* the picture: the last one SDLPoP presented, and it as BGRA */
	uint8_t rgb[POP_VIDEO_WIDTH * POP_VIDEO_HEIGHT * 3];
	uint32_t bgra[POP_VIDEO_WIDTH * POP_VIDEO_HEIGHT];

	/* the controller */
	uint8_t want[POP_BTN_COUNT];
	uint8_t held[POP_BTN_COUNT];

	/* the two stacks */
	void *game_sp, *host_sp;
	int on_game;

	/* what the game is started with */
	char ini[16384];
	int ini_len;
	char arg_seed[32];
	int digital_sound;
	char levels[256];         /* the "levels" slot's file, or empty */
	char *argv[4];
	long steps;               /* FrameAdvance calls so far */
} pop_driver;

static pop_driver g;

static const char *custom_levels(void) { return g.levels[0] ? g.levels : NULL; }

int popdrv_on_game_stack(void) { return g.on_game; }

/* --------------------------------------------------------------- the clock */

static uint64_t ceil_unit(uint64_t t)
{
	return (t + POP_COUNTS_PER_UNIT - 1) / POP_COUNTS_PER_UNIT * POP_COUNTS_PER_UNIT;
}

uint64_t popdrv_clock(void) { return g.now; }

static void emit(const int16_t *samples, int n)
{
	int room = POP_AUDIO_MAX_SAMPLES - g.out_samples;
	if (n > room) n = room;
	if (n <= 0) return;
	if (samples)
		memcpy(g.out + g.out_samples * 2, samples, (size_t)n * 4);
	else
		memset(g.out + g.out_samples * 2, 0, (size_t)n * 4);
	g.out_samples += n;
}

/* The sound of [from, to): the callback is asked for 1024 samples at a time,
 * as SDL's device asks, and only while the device is playing. */
static void run_audio(uint64_t from, uint64_t to)
{
	uint64_t n = to / POP_COUNTS_PER_SAMPLE - from / POP_COUNTS_PER_SAMPLE;
	while (n)
	{
		if (!g.audio_open || g.audio_paused || g.over)
		{
			while (n) { int k = n > 4096 ? 4096 : (int)n; emit(NULL, k); n -= (uint64_t)k; }
			break;
		}
		if (g.block_pos == 1024)
		{
			g.callback(g.callback_user, (Uint8 *)g.block, (int)sizeof g.block);
			g.block_pos = 0;
		}
		int take = 1024 - g.block_pos;
		if ((uint64_t)take > n) take = (int)n;
		emit(g.block + g.block_pos * 2, take);
		g.block_pos += take;
		n -= (uint64_t)take;
	}
}

/* The end of a step: the sound of the wait, then back to FrameAdvance. When the
 * next FrameAdvance switches back in, the wait is over: the clock is at its
 * end, and the game's own event pump takes the input that step brought. */
static void end_step_no_pump(uint64_t target)
{
	if (target <= g.now) target = ceil_unit(g.now + 1);
	run_audio(g.now, target);
	g.step_length = target - g.step_start;
	g.on_game = 0;
	chimera_co_switch(&g.game_sp, g.host_sp);
	g.now = target;
	g.step_start = target;
	g.idle_pumps = 0;
}

static void end_step(uint64_t target)
{
	end_step_no_pump(target);
	if (!g.over)
		process_events();
	g.idle_pumps = 0;
}

void popdrv_delay(uint32_t ms)
{
	uint64_t target = ceil_unit(g.now + (uint64_t)(ms ? ms : 1) * POP_COUNTS_PER_MS);
	if (g.pending_until > target) target = g.pending_until;
	g.pending_until = 0;
	end_step(target);
}

/* ----------------------------------------------------------------- the hooks */

void chimera_wait_for_timer(int timer_index)
{
	/* has_timer_stopped() counts whole timer ticks since the timer's last
	 * counter; this is the first counter at which that count reaches its wait */
	const uint64_t per = perf_counters_per_tick;
	g.pending_until = (timer_last_counter[timer_index] / per + (uint64_t)wait_time[timer_index]) * per;
}

void chimera_input_read(void) { g.input_read = 1; }

void chimera_idle_step(void) { end_step(ceil_unit(g.now + 1)); }

/* The safety net under the hooks: a loop that keeps pumping events without
 * the clock moving is waiting for something only time brings (a sound to end,
 * a key). SDLPoP spins such loops at full speed and real time ends them; here
 * nothing would. After 64 empty pumps the loop gets a 1/60 s step, which is
 * what each of the hooked ones gets per pass. The caller (seams.c's
 * SDL_PollEvent) polls again, so it sees the new step's input at once. */
int popdrv_idle_pump(void)
{
	if (!g.on_game || g.over || ++g.idle_pumps < 64)
		return 0;
	if (!g.spin_reported)
	{
		g.spin_reported = 1;
		fprintf(stderr, "[sdlpop] a loop waited without a hook; it is stepped at 60 Hz\n");
	}
	end_step_no_pump(ceil_unit(g.now + 1));
	return 1;
}

/* ------------------------------------------------- what seams.c hands over */

void popdrv_audio_open(SDL_AudioCallback cb, void *user)
{
	g.callback = cb;
	g.callback_user = user;
	g.audio_open = 1;
	g.audio_paused = 1; /* an SDL device starts paused */
}

void popdrv_audio_close(void) { g.audio_open = 0; }
void popdrv_audio_pause(int paused) { g.audio_paused = paused != 0; }
int popdrv_audio_status(void) { return !g.audio_open ? 0 : g.audio_paused ? 2 : 1; }

void popdrv_present(const void *pixels, int pitch)
{
	const uint8_t *src = (const uint8_t *)pixels;
	for (int y = 0; y < POP_VIDEO_HEIGHT; y++)
		memcpy(g.rgb + y * POP_VIDEO_WIDTH * 3, src + (size_t)y * (size_t)pitch, POP_VIDEO_WIDTH * 3);
}

const char *popdrv_ini(int *len)
{
	*len = g.ini_len;
	return g.ini;
}

void popdrv_game_exit(int code)
{
	if (!g.on_game)
		__real_exit(code);
	g.over = 1;
	g.exit_code = code;
	fprintf(stderr, "[sdlpop] the game stopped (exit %d)\n", code);
	for (;;)
		end_step(ceil_unit(g.now + 1));
}

/* ------------------------------------------------------------- the game side */

void chimera_co_entry(void)
{
	int argc = 0;
	g.argv[argc++] = "prince";
	g.argv[argc++] = g.arg_seed;
	if (!g.digital_sound) g.argv[argc++] = "stdsnd"; /* the PC speaker */
	g.argv[argc] = NULL;
	g_argc = argc;
	g_argv = g.argv;
	pop_main();
	exit(0);
}

/* --------------------------------------------------------------- settings */

typedef struct
{
	const char *name;
	int is_bool;
	const char *section;
	long dflt, min, max;
} pop_setting;

static const pop_setting k_settings[] = {
#define POP_SETTING(name, kind, section, dflt, min, max, display, desc) \
	{ #name, POP_KIND_##kind, section, dflt, min, max },
#define POP_KIND_BOOL 1
#define POP_KIND_INT 0
#include "settings.inc"
#undef POP_SETTING
};
#define POP_SETTING_COUNT ((int)(sizeof k_settings / sizeof k_settings[0]))

static void ini_add(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(g.ini + g.ini_len, sizeof g.ini - (size_t)g.ini_len, fmt, ap);
	va_end(ap);
	if (n > 0 && g.ini_len + n < (int)sizeof g.ini) g.ini_len += n;
}

/* SDLPoP.ini as the game will read it: what the settings say, and everything
 * that is not a setting pinned to the original game - no SDLPoP info screen,
 * no pause menu, no quicksaves or replays (keys a movie never presses and files
 * a sandbox never writes), no lighting, and the original's fades, flashes and
 * texts, which change how many steps a scene takes. */
static void build_ini(void)
{
	static const char *const sections[] = { "General", "AdditionalFeatures", "Enhancements", "CustomGameplay" };
	g.ini_len = 0;
	for (int s = 0; s < 4; s++)
	{
		ini_add("[%s]\n", sections[s]);
		if (s == 0)
			ini_add("enable_pause_menu = false\nenable_music = true\nenable_fade = true\nenable_flash = true\n"
			        "enable_text = true\nenable_info_screen = false\nstart_fullscreen = false\n"
			        "use_hardware_acceleration = false\nuse_correct_aspect_ratio = false\nuse_integer_scaling = false\n"
			        "scaling_type = sharp\nenable_controller_rumble = false\nlevelset = original\n"
			        "always_use_original_music = false\nalways_use_original_graphics = false\n");
		if (s == 1)
			ini_add("enable_quicksave = false\nenable_quicksave_penalty = false\nenable_replay = false\nenable_lighting = false\n");
		if (s == 3)
			ini_add("use_custom_options = true\n");
		for (int i = 0; i < POP_SETTING_COUNT; i++)
		{
			const pop_setting *st = &k_settings[i];
			if (strcmp(st->section, sections[s]) != 0) continue;
			if (st->is_bool)
				ini_add("%s = %s\n", st->name, wbx_setting_bool(st->name, (int)st->dflt) ? "true" : "false");
			else
			{
				long v = wbx_setting_long(st->name, st->dflt);
				if (v < st->min) v = st->min;
				if (v > st->max) v = st->max;
				ini_add("%s = %ld\n", st->name, v);
			}
		}
	}
	/* SDLPoP's reader expects a comment after the last option; without one it
	 * reports a short read (having read every option all the same) */
	ini_add("; end\n");
}

/* ------------------------------------------------------------------ Init */

static int check_files(char *err, int errsize)
{
	char missing[512] = "", wrong[1024] = "";
	int nmissing = 0;
	for (int i = 0; i < POP_FILE_COUNT; i++)
	{
		const pop_file *pf = &k_files[i];
		if (pf->need == NEED_DIGITAL && !g.digital_sound) continue;
		if (pf->need == NEED_ORIGINAL_LEVELS && custom_levels()) continue;
		FILE *f = __real_fopen(pf->name, "rb");
		if (!f)
		{
			size_t len = strlen(missing);
			snprintf(missing + len, sizeof missing - len, "%s%s", nmissing ? ", " : "", pf->name);
			nmissing++;
			continue;
		}
		char hex[41];
		long size = 0;
		int ok = sha1_file(f, hex, &size);
		fclose(f);
		if (ok && !strcmp(hex, pf->sha1)) continue;
		if (wrong[0]) continue; /* the first is enough to say what is wrong */
		const char *what = NULL;
		for (size_t k = 0; k < sizeof k_other / sizeof k_other[0]; k++)
			if (!strcmp(hex, k_other[k].sha1)) what = k_other[k].what;
		if (what)
			snprintf(wrong, sizeof wrong, "%s is %s, not Prince of Persia 1.0's - this core plays the 1.0 data and checks it file by file. Add the 1.0 %s.",
			         pf->name, what, pf->name);
		else
			snprintf(wrong, sizeof wrong, "%s is not Prince of Persia 1.0's (%ld bytes, SHA-1 %s; 1.0's is %ld bytes, %s) - this core plays the 1.0 data and checks it file by file. Add the 1.0 %s.",
			         pf->name, size, ok ? hex : "unreadable", pf->size, pf->sha1, pf->name);
	}
	if (nmissing)
	{
		snprintf(err, (size_t)errsize, "Prince of Persia needs %s - add %s as the project's firmware.",
		         missing, nmissing > 1 ? "them" : "it");
		return 0;
	}
	if (wrong[0])
	{
		snprintf(err, (size_t)errsize, "%s", wrong);
		return 0;
	}
	return 1;
}

int popdrv_init(char *err, int errsize)
{
	memset(&g, 0, sizeof g);

	char sound[32];
	if (wbx_setting_str("sound", sound, sizeof sound) < 0) strcpy(sound, "digital");
	if (!strcmp(sound, "digital")) g.digital_sound = 1;
	else if (!strcmp(sound, "pcSpeaker")) g.digital_sound = 0;
	else
	{
		snprintf(err, (size_t)errsize, "unknown sound setting '%s'", sound);
		return 0;
	}
	unsigned long seed = (unsigned long)wbx_setting_long("random_seed", 0) & 0xFFFFFFFFul;
	snprintf(g.arg_seed, sizeof g.arg_seed, "seed=%lu", seed);

	/* a level set of the project's own: played in place of LEVELS.DAT, and
	 * not held to 1.0's hash (it is meant to differ) */
	if (wbx_slot_first("levels", g.levels, sizeof g.levels))
	{
		FILE *f = __real_fopen(g.levels, "rb");
		if (!f)
		{
			snprintf(err, (size_t)errsize, "the project's level set %s is not there", g.levels);
			return 0;
		}
		fclose(f);
	}
	if (!check_files(err, errsize))
		return 0;
	build_ini();
	if (!gamestate_init(err, errsize))
		return 0;

	g.block_pos = 1024;
	g.game_sp = chimera_co_create(4u << 20);
	if (!g.game_sp)
	{
		snprintf(err, (size_t)errsize, "no memory for the game's stack");
		return 0;
	}

	/* the game starts: it loads its data and runs to its first wait */
	g.on_game = 1;
	chimera_co_switch(&g.host_sp, g.game_sp);
	if (g.over)
	{
		snprintf(err, (size_t)errsize, "the game stopped while starting (exit %d)", g.exit_code);
		return 0;
	}
	g.started = 1;
	gamestate_from_game();
	return 1;
}

/* --------------------------------------------------------------- the step */

void popdrv_set_button(int index, int down)
{
	if (index >= 0 && index < POP_BTN_COUNT) g.want[index] = down ? 1 : 0;
}

static void push_key(int down, SDL_Scancode sc, Uint16 mod)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = sc;
	e.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(sc);
	e.key.keysym.mod = mod;
	SDL_PushEvent(&e);
}

/* The buttons that changed, as the key events a keyboard would have sent, in
 * the panel's order. Shift and the Ctrl of Ctrl+A are real modifiers: a key
 * pressed while they are down carries them. */
static void push_input(void)
{
	static const SDL_Scancode k_scancode[POP_BTN_COUNT] = {
		SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
		SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RETURN, SDL_SCANCODE_A,
	};
	for (int i = 0; i < POP_BTN_COUNT; i++)
	{
		if (g.want[i] == g.held[i]) continue;
		const int down = g.want[i];
		g.held[i] = g.want[i];
		const Uint16 mod = (g.held[POP_BTN_SHIFT] ? KMOD_LSHIFT : 0) | (g.held[POP_BTN_RESTART_LEVEL] ? KMOD_LCTRL : 0);
		if (i == POP_BTN_RESTART_LEVEL)
		{
			/* Ctrl+A: Ctrl goes down first and comes up last */
			if (down)
			{
				push_key(1, SDL_SCANCODE_LCTRL, mod);
				push_key(1, SDL_SCANCODE_A, mod);
			}
			else
			{
				push_key(0, SDL_SCANCODE_A, mod);
				push_key(0, SDL_SCANCODE_LCTRL, mod);
			}
		}
		else
			push_key(down, k_scancode[i], mod);
	}
}

void popdrv_frame(int render)
{
	g.steps++;
	gamestate_to_game();
	push_input();
	g.out_samples = 0;
	g.input_read = 0;

	g.on_game = 1;
	chimera_co_switch(&g.host_sp, g.game_sp);

	gamestate_from_game();
	if (render)
	{
		const uint8_t *s = g.rgb;
		for (int i = 0; i < POP_VIDEO_WIDTH * POP_VIDEO_HEIGHT; i++, s += 3)
			g.bgra[i] = 0xFF000000u | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
	}
}

const uint32_t *popdrv_video(void) { return g.bgra; }

const int16_t *popdrv_audio(int *samples)
{
	*samples = g.out_samples;
	return g.out;
}

int popdrv_input_was_read(void) { return g.input_read; }

void popdrv_vsync(int *num, int *den)
{
	/* before any step, the rate of play (the engine reads the rate once, after
	 * Init, and the title's first step says nothing about the game) */
	if (g.steps == 0) { *num = 12; *den = 1; return; }
	uint64_t a = POP_CLOCK_HZ, b = g.step_length ? g.step_length : POP_COUNTS_PER_UNIT;
	uint64_t x = a, y = b;
	while (y) { uint64_t t = x % y; x = y; y = t; }
	*num = (int)(a / x);
	*den = (int)(b / x);
}
