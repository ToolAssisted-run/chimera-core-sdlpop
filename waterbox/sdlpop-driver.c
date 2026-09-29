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
 * THE DATA are the user's own Prince of Persia files, of the release the
 * version setting names (1.0, 1.1, 1.3 or 1.4), mounted by name as the
 * project's firmware. Init checks each one the game will open by SHA-1, and
 * the release's PRINCE.EXE, which the game never runs but which is what tells
 * 1.0 from 1.1 and 1.3 from 1.4 (their data files are the same); it refuses a
 * missing file or one of another release by name. The game can open nothing
 * else (seams.c), so it cannot fall back to SDLPoP's extracted folders.
 *
 * THE RELEASE is more than its files. What differs in the program, SDLPoP
 * already knows how to be told, and the core tells it (build_ini, and patch
 * 0002 for the copy protection): the guards' fighting tables (1.0 has its own;
 * 1.1, 1.3 and 1.4 share the later one - read from each PRINCE.EXE), the level
 * colours (1.3 and 1.4 only), the copy protection's manual (1.0, 1.1, and
 * 1.3/1.4 each ask other words) and the line Ctrl+V shows. Nothing else of
 * what SDLPoP can read from a DOS PRINCE.EXE differs between the releases.
 *
 * NOTHING ELSE IS SDLPoP'S: every fix and enhancement is off unless the
 * project turns it on, and the cheats need their setting, as the DOS game
 * needed its cheat word on the command line.
 */
#include "common.h"

#include <errno.h>

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

enum { NEED_ALWAYS, NEED_DIGITAL, NEED_ORIGINAL_LEVELS, NEED_CHECK_ONLY };

/* the releases, as a mask over them */
enum { V10 = 1, V11 = 2, V13 = 4, V14 = 8 };

typedef struct
{
	const char *name;
	int releases;
	long size;
	const char *sha1;
	int need;
} pop_file;

/* Prince of Persia (DOS), file by file, for each release: the user's own
 * copies of the four, hashed. 1.0 and 1.1 have the same data files, and so do
 * 1.3 and 1.4 (the 1.4 CD's); PRINCE.EXE is each release's own and is only
 * checked - SDLPoP is the program. The digitized sounds and the AdLib music
 * are needed only with the Sound Blaster; the PC speaker's are always opened,
 * as SDLPoP opens them whatever the sound card. The levels are the original
 * ones unless the project brings a level set of its own. */
static const pop_file k_files[] = {
	{ "PRINCE.DAT", V10|V11, 3278, "A0CD3C9699ACF510A2EBE2D0D675B837D2CE73C8", NEED_ALWAYS },
	{ "PRINCE.DAT", V13|V14, 25600, "57FEDE2FBF1BB4A211066393353B6A2E0060B9BD", NEED_ALWAYS },
	{ "KID.DAT", V10|V11|V13|V14, 37149, "C7E841443C247BF65FF94C13A4B01FDACA767DCA", NEED_ALWAYS },
	{ "LEVELS.DAT", V10|V11|V13|V14, 37031, "2CD58BB1EECE062EF98FD8411377B58F3C72C031", NEED_ORIGINAL_LEVELS },
	{ "TITLE.DAT", V10|V11, 36725, "0923E93BCC06C2E0802A4B853E1544EE7E5BB83E", NEED_ALWAYS },
	{ "TITLE.DAT", V13|V14, 36799, "8C6E587317B34F6A7E79798DF38731B693138B42", NEED_ALWAYS },
	{ "PV.DAT", V10|V11|V13|V14, 25829, "DA20D2C8ED3DA6E93CC882E7023BD502E0FC7BF9", NEED_ALWAYS },
	{ "VDUNGEON.DAT", V10|V11, 14220, "5D9B304EF79336F34E66AEF209439A097E3D15D4", NEED_ALWAYS },
	{ "VDUNGEON.DAT", V13|V14, 10933, "9F71FA4D364175035FB07B4396AAE21E2CFDF41B", NEED_ALWAYS },
	{ "VPALACE.DAT", V10|V11, 14226, "63707A2FD116027B410031554B7FD68AB05C5D34", NEED_ALWAYS },
	{ "VPALACE.DAT", V13|V14, 14212, "09F261C0493C90D8FA16123C5215A881E138FA58", NEED_ALWAYS },
	{ "GUARD.DAT", V10|V11|V13|V14, 6950, "900C3B2EB4D84DC45E6FD086AE15B93347694103", NEED_ALWAYS },
	{ "GUARD1.DAT", V10|V11|V13|V14, 117, "1F3BD8B571D7E42F230D6D4740B6EC6A81400AF0", NEED_ALWAYS },
	{ "GUARD2.DAT", V10|V11|V13|V14, 117, "32540281AF1594610D2CB83D41F1B5691E89B50D", NEED_ALWAYS },
	{ "FAT.DAT", V10|V11|V13|V14, 6521, "CABACC01E292A54BAE75A7C3514B10231A575454", NEED_ALWAYS },
	{ "SKEL.DAT", V10|V11, 3864, "EFAC96CDC39B9897F33D0186D683972E5D0CCBC8", NEED_ALWAYS },
	{ "SKEL.DAT", V13|V14, 3868, "B02DDFF312ED99EF529D3651FFFD475E6F27C540", NEED_ALWAYS },
	{ "VIZIER.DAT", V10|V11|V13|V14, 6111, "F3BBE710982BBCDB51F43AB5C97D0E23EFD0F633", NEED_ALWAYS },
	{ "SHADOW.DAT", V10|V11|V13|V14, 4715, "CBC976A65730CA2B8F14A7CF79CA14F413701053", NEED_ALWAYS },
	{ "IBM_SND1.DAT", V10|V11|V13|V14, 3684, "92F68AFE878FA233D44BB0338DE374C2C37264B9", NEED_ALWAYS },
	{ "IBM_SND2.DAT", V10|V11|V13|V14, 3784, "CF74CAE4A55521A41E618C1A21A0C6AD92B0412E", NEED_ALWAYS },
	{ "DIGISND1.DAT", V10|V11, 48545, "5EAB876D2AEB62447DD6957144771E0F1BE56D0D", NEED_DIGITAL },
	{ "DIGISND1.DAT", V13|V14, 50101, "27DC74B67E3EF9E261762B9D36DC3E01F9C318F5", NEED_DIGITAL },
	{ "DIGISND2.DAT", V10|V11, 29143, "62E225351D23338419750AFE82BA8506529521B4", NEED_DIGITAL },
	{ "DIGISND2.DAT", V13|V14, 32426, "CB5B90C548233F7D48F85769AA8F96D55A2CE087", NEED_DIGITAL },
	{ "DIGISND3.DAT", V10|V11, 31000, "5708C6171F67994993BD210ED28101F752591D22", NEED_DIGITAL },
	{ "DIGISND3.DAT", V13|V14, 31008, "41CB8EAF953E1D54BD9A57727FB3E9CD6B029492", NEED_DIGITAL },
	{ "MIDISND1.DAT", V10|V11, 9368, "03FDED77B3A2DDD6487041159796CB835F4D945B", NEED_DIGITAL },
	{ "MIDISND1.DAT", V13|V14, 7096, "30BBCC0C3E893DBA2E4288F600294CC5BA1FC11A", NEED_DIGITAL },
	{ "MIDISND2.DAT", V10|V11, 18408, "DB1A145AB96C24156944B43DFC229F5424C65856", NEED_DIGITAL },
	{ "MIDISND2.DAT", V13|V14, 18958, "8320C09113C4E3B31F00959402F79F31738B09E3", NEED_DIGITAL },
	{ "PRINCE.EXE", V10, 123335, "587AA625ABF962B44A98C42AD69693576FB83505", NEED_CHECK_ONLY },
	{ "PRINCE.EXE", V11, 122135, "C980F942FC7AD27CD42A40A64C75BA1D43513947", NEED_CHECK_ONLY },
	{ "PRINCE.EXE", V13, 125115, "205252335E188B1A56C3FB170DA8C124955A9AF4", NEED_CHECK_ONLY },
	{ "PRINCE.EXE", V14, 110855, "4D257E60684DAFFA4D0BF78DE876113CDC34064E", NEED_CHECK_ONLY },
};
#define POP_FILE_COUNT ((int)(sizeof k_files / sizeof k_files[0]))

/* A file known to be something else under the same name: SDLPoP's repository
 * ships this sound file, which is no release's. */
static const struct { const char *sha1; const char *what; } k_other[] = {
	{ "9E1464DAC4078B1754DB25E0DC3A9CD245DE8FE4", "the one SDLPoP ships" },
};

/* ------------------------------------------------------------- the releases */

typedef struct
{
	const char *name;       /* the version setting's value */
	int mask;
	const char *version_line; /* Ctrl+V, as the release's PRINCE.EXE has it */
	int later_guards;       /* the guards' later fighting tables (below) */
	int level_colours;      /* 1.3's level colour variations */
	/* the copy protection: the manual's page, line and word for each of the
	 * 40 questions, and the letter each answer starts with */
	const char *letters;
	uint16_t page[40], line[40], word[40];
	int page_first;         /* the question on screen asks the page first */
} pop_release;

/* Read out of each release's PRINCE.EXE (its EXEPACK unpacked): the tables lie
 * page, line, word, letters, one after the other. */
static const pop_release k_releases[] = {
	{ "1.0", V10, "PRINCE OF PERSIA  V1.0", 0, 0,
	  "AABBCCDDEFFGHHIIJJKLLMMNOOPPRRSSTTUUVYWY",
	  { 5, 3, 7, 3, 3, 4, 1, 5, 12, 5, 11, 10, 1, 2, 8, 8, 2, 4, 6, 1, 4, 7, 3, 2, 1, 7, 10, 1, 4, 3, 4, 1, 4, 1, 8, 1, 1, 10, 3, 3 },
	  { 2, 1, 5, 4, 3, 5, 1, 3, 7, 2, 2, 4, 6, 6, 2, 6, 3, 1, 2, 3, 2, 2, 3, 10, 5, 6, 5, 6, 3, 5, 7, 2, 2, 4, 5, 7, 2, 6, 5, 5 },
	  { 9, 1, 6, 4, 5, 3, 6, 3, 4, 4, 3, 2, 12, 5, 13, 1, 9, 2, 2, 4, 9, 4, 11, 8, 5, 4, 1, 6, 2, 4, 6, 8, 4, 2, 7, 11, 5, 4, 1, 2 },
	  0 },
	{ "1.1", V11, "PRINCE OF PERSIA  V1.1", 1, 0,
	  "AABBCCDDEFFGHHIIJJKLLMMNOOPPRRSSTTUUVWYY",
	  { 1, 3, 5, 4, 4, 2, 1, 5, 6, 4, 3, 5, 6, 2, 5, 2, 2, 2, 6, 5, 4, 4, 1, 6, 5, 3, 3, 1, 2, 3, 3, 5, 6, 5, 1, 6, 1, 1, 2, 1 },
	  { 5, 2, 2, 8, 3, 1, 1, 4, 6, 5, 2, 5, 3, 5, 2, 3, 2, 1, 3, 1, 6, 2, 8, 4, 7, 8, 6, 6, 3, 8, 1, 3, 4, 7, 3, 4, 2, 1, 9, 7 },
	  { 12, 9, 4, 4, 3, 4, 6, 7, 4, 7, 4, 4, 2, 5, 13, 6, 6, 2, 1, 1, 5, 4, 1, 2, 5, 9, 3, 6, 2, 2, 1, 2, 9, 1, 8, 6, 5, 7, 5, 3 },
	  1 },
	{ "1.3", V13, "PRINCE OF PERSIA  V1.3", 1, 1,
	  "WOESPBYSKJTBCFESKMMTPYKCGSULJCDILTTAMCSG",
	  { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 10, 10, 10, 10, 11, 11, 11, 11, 13, 13, 13, 13 },
	  { 2, 5, 8, 10, 2, 3, 6, 1, 4, 5, 6, 1, 2, 3, 6, 11, 1, 2, 3, 3, 1, 1, 2, 2, 3, 3, 4, 7, 1, 1, 5, 3, 2, 3, 4, 5, 3, 4, 4, 6 },
	  { 2, 4, 6, 6, 5, 8, 6, 2, 9, 1, 3, 7, 5, 3, 1, 9, 5, 1, 2, 1, 1, 4, 2, 8, 4, 8, 2, 5, 2, 4, 4, 3, 5, 3, 1, 5, 3, 5, 8, 2 },
	  1 },
	{ "1.4", V14, "PRINCE OF PERSIA  V1.4", 1, 1,
	  "WOESPBYSKJTBCFESKMMTPYKCGSULJCDILTTAMCSG",
	  { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 10, 10, 10, 10, 11, 11, 11, 11, 13, 13, 13, 13 },
	  { 2, 5, 8, 10, 2, 3, 6, 1, 4, 5, 6, 1, 2, 3, 6, 11, 1, 2, 3, 3, 1, 1, 2, 2, 3, 3, 4, 7, 1, 1, 5, 3, 2, 3, 4, 5, 3, 4, 4, 6 },
	  { 2, 4, 6, 6, 5, 8, 6, 2, 9, 1, 3, 7, 5, 3, 1, 9, 5, 1, 2, 1, 1, 4, 2, 8, 4, 8, 2, 5, 2, 4, 4, 3, 5, 3, 1, 5, 3, 5, 8, 2 },
	  1 },
};
#define POP_RELEASE_COUNT ((int)(sizeof k_releases / sizeof k_releases[0]))

/* The guards' fighting, per skill (0-11): 1.0's tables, and the later ones
 * 1.1, 1.3 and 1.4 share - read from each PRINCE.EXE, and as CusPop and
 * quickerSDLPoP give them. Block, advance and extra strength are the same in
 * both. */
static const uint16_t k_guards[2][7][12] = {
	{
		{ 61, 100, 61, 61, 61, 40, 100, 220, 0, 48, 32, 48 },       /* strikeprob */
		{ 0, 0, 0, 5, 5, 175, 16, 8, 0, 255, 255, 150 },            /* restrikeprob */
		{ 0, 150, 150, 200, 200, 255, 200, 250, 0, 255, 255, 255 }, /* blockprob */
		{ 0, 61, 61, 100, 100, 145, 100, 250, 0, 145, 255, 175 },   /* impblockprob */
		{ 255, 200, 200, 200, 255, 255, 200, 0, 0, 255, 100, 100 }, /* advprob */
		{ 16, 16, 16, 16, 8, 8, 8, 8, 0, 8, 0, 0 },                 /* refractimer */
		{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },                     /* extrastrength */
	},
	{
		{ 75, 100, 75, 75, 75, 50, 100, 220, 0, 60, 40, 60 },
		{ 0, 0, 0, 5, 5, 175, 20, 10, 0, 255, 255, 150 },
		{ 0, 150, 150, 200, 200, 255, 200, 250, 0, 255, 255, 255 },
		{ 0, 75, 75, 100, 100, 145, 100, 250, 0, 145, 255, 175 },
		{ 255, 200, 200, 200, 255, 255, 200, 0, 0, 255, 100, 100 },
		{ 20, 20, 20, 20, 10, 10, 10, 10, 0, 10, 0, 0 },
		{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	},
};
static const char *const k_guard_fields[7] = {
	"strikeprob", "restrikeprob", "blockprob", "impblockprob", "advprob", "refractimer", "extrastrength",
};

/* 1.3's level colours: the variation each level's walls take (0 = none), as
 * the 1.3 and 1.4 PRINCE.EXE have them. 1.0 and 1.1 have none, and their
 * PRINCE.DAT has no palettes for any. */
static const int k_level_colours[16] = { 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 0, 0, 3, 3, 4, 0 };

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
		if (k_files[i].need != NEED_CHECK_ONLY && !strcmp(name, k_files[i].name)) return name;
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

	/* the controller: the buttons, and the keys they hold down on the
	 * keyboard SDL reports (the modifiers are shared: Shift is down while the
	 * Shift button or any Shift+ command is) */
	uint8_t want[POP_BTN_COUNT];
	uint8_t held[POP_BTN_COUNT];
	uint8_t keyboard[SDL_NUM_SCANCODES];
	int shift_down, ctrl_down;

	/* SDL's timers, on the game's clock (Shift+L starts one) */
	struct { int id; uint64_t due; uint32_t interval; SDL_TimerCallback cb; void *param; } timers[4];
	int next_timer_id;

	/* the files the game writes (PRINCE.SAV, PRINCE.HOF, QUICKSAVE.SAV) */
	struct { char *data; size_t size; int exists; } saves[3];

	/* the two stacks */
	void *game_sp, *host_sp;
	int on_game;

	/* what the game is started with */
	char ini[16384];
	int ini_len;
	char arg_seed[32];
	int digital_sound;
	char levels[256];         /* the "levels" slot's file, or empty */
	const pop_release *release;
	int cheats;               /* the game started with its cheat word */
	int from_savestate;       /* the run starts from the "savestate" slot's quicksave */
	char *argv[5];
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

/* SDL's timers run on a thread of their own; here they run on the game's
 * clock, on the game's stack, when a step that reaches their time resumes -
 * before the step's input is taken, as a thread's would have run while the
 * game waited. SDLPoP starts one: Shift+L gives the Shift keys back after
 * 250 ms if they are still held (so a level skipped with Shift held skips its
 * cutscene too). */
int popdrv_add_timer(uint32_t interval_ms, void *callback, void *param)
{
	for (int i = 0; i < (int)(sizeof g.timers / sizeof g.timers[0]); i++)
	{
		if (g.timers[i].id) continue;
		if (++g.next_timer_id <= 0) g.next_timer_id = 1;
		g.timers[i].id = g.next_timer_id;
		g.timers[i].interval = interval_ms;
		g.timers[i].due = g.now + (uint64_t)interval_ms * POP_COUNTS_PER_MS;
		g.timers[i].cb = (SDL_TimerCallback)callback;
		g.timers[i].param = param;
		return g.timers[i].id;
	}
	return 0;
}

int popdrv_remove_timer(int id)
{
	for (int i = 0; i < (int)(sizeof g.timers / sizeof g.timers[0]); i++)
		if (id && g.timers[i].id == id) { g.timers[i].id = 0; return 1; }
	return 0;
}

static void run_timers(void)
{
	for (int i = 0; i < (int)(sizeof g.timers / sizeof g.timers[0]); i++)
	{
		if (!g.timers[i].id || g.timers[i].due > g.now) continue;
		const Uint32 next = g.timers[i].cb(g.timers[i].interval, g.timers[i].param);
		if (next)
		{
			g.timers[i].interval = next;
			g.timers[i].due = g.now + (uint64_t)next * POP_COUNTS_PER_MS;
		}
		else
			g.timers[i].id = 0;
	}
}

/* SDL_GetKeyboardState: the keys the buttons hold down (push_key) */
const uint8_t *popdrv_keyboard_state(void) { return g.keyboard; }

/* The files the game writes - its saved game (Ctrl+G), its hall of fame and
 * SDLPoP's quicksave - live in guest memory, so a savestate carries them and
 * the host never sees them. SDLPoP puts them under its save path, which
 * seams.c's getenv makes "saves". The two slots put files here before the
 * game starts. */
static const char *const k_save_names[3] = { "PRINCE.SAV", "PRINCE.HOF", "QUICKSAVE.SAV" };
enum { SAVE_GAME, SAVE_HOF, SAVE_QUICK };

void *popdrv_save_file_open(const char *path, const char *mode, int *handled)
{
	*handled = 0;
	if (strncmp(path, "saves/", 6) != 0) return NULL;
	*handled = 1;
	for (int i = 0; i < 3; i++)
	{
		if (strcmp(path + 6, k_save_names[i]) != 0) continue;
		if (strchr(mode, 'a') || strchr(mode, '+')) break;
		if (strchr(mode, 'w'))
		{
			free(g.saves[i].data);
			g.saves[i].data = NULL;
			g.saves[i].size = 0;
			g.saves[i].exists = 1;
			return open_memstream(&g.saves[i].data, &g.saves[i].size);
		}
		if (!g.saves[i].exists || !g.saves[i].size)
		{
			errno = ENOENT;
			return NULL;
		}
		return fmemopen(g.saves[i].data, g.saves[i].size, "r");
	}
	errno = ENOENT;
	return NULL;
}

/* a slot's file, read into one of them; -1 if it cannot be read */
static long load_save(int which, const char *path)
{
	FILE *f = __real_fopen(path, "rb");
	if (!f) return -1;
	char *buf = NULL;
	size_t size = 0;
	FILE *m = open_memstream(&buf, &size);
	int c;
	while ((c = fgetc(f)) != EOF) fputc(c, m);
	fclose(f);
	fclose(m);
	g.saves[which].data = buf;
	g.saves[which].size = size;
	g.saves[which].exists = 1;
	return (long)size;
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
	run_timers();
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
	/* the cheats, as the DOS game took them: its cheat word on the command
	 * line (1.3 and 1.4's word was another; SDLPoP knows 1.0's) */
	if (g.cheats) g.argv[argc++] = "megahit";
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
		/* SDLPoP's quickload is what starts a run from the "savestate" slot;
		 * its keys (F6, F9) are on no button, so nothing else reaches it */
		if (s == 1)
			ini_add("enable_quicksave = %s\nenable_quicksave_penalty = false\nenable_replay = false\nenable_lighting = false\n",
			        g.from_savestate ? "true" : "false");
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
	/* the release: its level colours and its guards' fighting */
	for (int level = 0; level < 16; level++)
		ini_add("[Level %d]\nlevel_color = %d\n", level, g.release->level_colours ? k_level_colours[level] : 0);
	for (int skill = 0; skill < 12; skill++)
	{
		ini_add("[Skill %d]\n", skill);
		for (int f = 0; f < 7; f++)
			ini_add("%s = %d\n", k_guard_fields[f], k_guards[g.release->later_guards][f][skill]);
	}
	/* SDLPoP's reader expects a comment after the last option; without one it
	 * reports a short read (having read every option all the same) */
	ini_add("; end\n");
}

/* ------------------------------------------------------------------ Init */

/* the releases a mask names, as "1.0", "1.0/1.1" */
static const char *release_names(int mask, char *buf, size_t size)
{
	buf[0] = '\0';
	for (int r = 0; r < POP_RELEASE_COUNT; r++)
	{
		if (!(mask & k_releases[r].mask)) continue;
		size_t len = strlen(buf);
		snprintf(buf + len, size - len, "%s%s", len ? "/" : "", k_releases[r].name);
	}
	return buf;
}

static int check_files(char *err, int errsize)
{
	char missing[512] = "", wrong[1024] = "";
	int nmissing = 0;
	const char *rel = g.release->name;
	for (int i = 0; i < POP_FILE_COUNT; i++)
	{
		const pop_file *pf = &k_files[i];
		if (!(pf->releases & g.release->mask)) continue;
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
		/* another release's file, or something else known by that name */
		int other = 0;
		for (int k = 0; ok && k < POP_FILE_COUNT; k++)
			if (!strcmp(k_files[k].name, pf->name) && !strcmp(k_files[k].sha1, hex)) other |= k_files[k].releases;
		const char *what = NULL;
		for (size_t k = 0; ok && k < sizeof k_other / sizeof k_other[0]; k++)
			if (!strcmp(hex, k_other[k].sha1)) what = k_other[k].what;
		char names[32];
		if (other)
			snprintf(wrong, sizeof wrong, "%s is Prince of Persia %s's, not %s's - the project plays %s (the version setting). Add %s's %s, or set the version to the release your files are.",
			         pf->name, release_names(other, names, sizeof names), rel, rel, rel, pf->name);
		else if (what)
			snprintf(wrong, sizeof wrong, "%s is %s, not Prince of Persia %s's - this core plays the release's own data and checks it file by file. Add %s's %s.",
			         pf->name, what, rel, rel, pf->name);
		else
			snprintf(wrong, sizeof wrong, "%s is not Prince of Persia %s's (%ld bytes, SHA-1 %s; %s's is %ld bytes, %s) - this core plays the release's own data and checks it file by file. Add %s's %s.",
			         pf->name, rel, size, ok ? hex : "unreadable", rel, pf->size, pf->sha1, rel, pf->name);
	}
	if (nmissing)
	{
		snprintf(err, (size_t)errsize, "Prince of Persia %s needs %s - add %s as the project's firmware.",
		         rel, missing, nmissing > 1 ? "them" : "it");
		return 0;
	}
	if (wrong[0])
	{
		snprintf(err, (size_t)errsize, "%s", wrong);
		return 0;
	}
	return 1;
}

/* seg000.c: the copy protection's questions (writable under patch 0002), and
 * SDLPoP's quicksave, read to learn how long a quicksave of this build is */
extern word copyprot_word[40], copyprot_line[40], copyprot_page[40];
int quick_process(int (*process_func)(void *data, size_t data_size));
extern const char quick_version[9];

static size_t g_quick_size;
static int count_quick(void *data, size_t size)
{
	(void)data;
	g_quick_size += size;
	return 1;
}

/* patch 0002's hooks: the release's question on screen, and its Ctrl+V line */
int chimera_copyprot_page_first(void) { return g.release->page_first; }
const char *chimera_version_text(void) { return g.release->version_line; }

/* The two files a project may start from: a saved game of the original game's
 * own (PRINCE.SAV, which Load Game - Ctrl+L on the title - resumes) and an
 * SDLPoP quicksave (QUICKSAVE.SAV), which the run starts from. */
static int load_slots(char *err, int errsize)
{
	char path[256];
	if (wbx_slot_first("savegame", path, sizeof path))
	{
		long n = load_save(SAVE_GAME, path);
		if (n < 0)
		{
			snprintf(err, (size_t)errsize, "the project's saved game %s is not there", path);
			return 0;
		}
		/* the minutes, the ticks, the level and the hit points, two bytes each */
		if (n != 8)
		{
			snprintf(err, (size_t)errsize, "the project's saved game %s is %ld bytes; a Prince of Persia PRINCE.SAV is 8", path, n);
			return 0;
		}
	}
	if (wbx_slot_first("savestate", path, sizeof path))
	{
		long n = load_save(SAVE_QUICK, path);
		if (n < 0)
		{
			snprintf(err, (size_t)errsize, "the project's savestate %s is not there", path);
			return 0;
		}
		g_quick_size = sizeof quick_version;
		quick_process(count_quick);
		if ((size_t)n < sizeof quick_version || memcmp(g.saves[SAVE_QUICK].data, quick_version, sizeof quick_version) != 0)
		{
			snprintf(err, (size_t)errsize, "the project's savestate %s is not an SDLPoP quicksave (QUICKSAVE.SAV, which starts \"%s\")", path, quick_version);
			return 0;
		}
		if ((size_t)n != g_quick_size)
		{
			snprintf(err, (size_t)errsize, "the project's savestate %s is %ld bytes; a quicksave of this SDLPoP (%s) is %zu - it was made by another SDLPoP build",
			         path, n, SDLPOP_VERSION, g_quick_size);
			return 0;
		}
		g.from_savestate = 1;
	}
	return 1;
}

int popdrv_init(char *err, int errsize)
{
	memset(&g, 0, sizeof g);

	char version[16];
	if (wbx_setting_str("version", version, sizeof version) < 0) strcpy(version, "1.0");
	for (int r = 0; r < POP_RELEASE_COUNT; r++)
		if (!strcmp(version, k_releases[r].name)) g.release = &k_releases[r];
	if (!g.release)
	{
		snprintf(err, (size_t)errsize, "unknown version setting '%s' (1.0, 1.1, 1.3 or 1.4)", version);
		return 0;
	}
	g.cheats = wbx_setting_bool("cheats", 0) != 0;

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
	if (!load_slots(err, errsize))
		return 0;
	build_ini();

	/* the release's manual, before the game picks its questions */
	memcpy(copyprot_letter, g.release->letters, 40);
	memcpy(copyprot_page, g.release->page, sizeof copyprot_page);
	memcpy(copyprot_line, g.release->line, sizeof copyprot_line);
	memcpy(copyprot_word, g.release->word, sizeof copyprot_word);
	/* A run from a savestate: SDLPoP's quickload takes it on the level's
	 * first tick, as F9 on the title does - which also names a level to start,
	 * as the DOS game's own "prince 5" did, so the title is not shown. (Not
	 * skip_title: that one leaves no level named, and the first key then
	 * starts the game again from the first level, as on the title.) */
	if (g.from_savestate)
	{
		long first = wbx_setting_long("first_level", 1);
		start_level = (short)(first < 1 ? 1 : first > 14 ? 14 : first);
		need_quick_load = 1;
	}
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

int popdrv_button_active(int index)
{
	if (index < 0 || index >= POP_BTN_COUNT) return 0;
	return index < POP_BTN_CHEAT_FIRST || g.cheats;
}

void popdrv_set_button(int index, int down)
{
	if (popdrv_button_active(index)) g.want[index] = down ? 1 : 0;
}

/* What each button is on the DOS keyboard: a key, and the modifiers held with
 * it. Shift itself is the Shift key. */
enum { MOD_SHIFT = 1, MOD_CTRL = 2 };
static const struct { SDL_Scancode key; int mods; } k_keys[POP_BTN_COUNT] = {
	[POP_BTN_UP] = { SDL_SCANCODE_UP, 0 },
	[POP_BTN_DOWN] = { SDL_SCANCODE_DOWN, 0 },
	[POP_BTN_LEFT] = { SDL_SCANCODE_LEFT, 0 },
	[POP_BTN_RIGHT] = { SDL_SCANCODE_RIGHT, 0 },
	[POP_BTN_SHIFT] = { SDL_SCANCODE_LSHIFT, 0 },
	[POP_BTN_ENTER] = { SDL_SCANCODE_RETURN, 0 },
	[POP_BTN_PAUSE] = { SDL_SCANCODE_ESCAPE, 0 },
	[POP_BTN_SHOW_TIME] = { SDL_SCANCODE_SPACE, 0 },
	[POP_BTN_RESTART_LEVEL] = { SDL_SCANCODE_A, MOD_CTRL },
	[POP_BTN_RESTART_GAME] = { SDL_SCANCODE_R, MOD_CTRL },
	[POP_BTN_NEXT_LEVEL] = { SDL_SCANCODE_L, MOD_SHIFT },
	[POP_BTN_SAVE_GAME] = { SDL_SCANCODE_G, MOD_CTRL },
	[POP_BTN_LOAD_GAME] = { SDL_SCANCODE_L, MOD_CTRL },
	[POP_BTN_SOUND_ON_OFF] = { SDL_SCANCODE_S, MOD_CTRL },
	[POP_BTN_VERSION] = { SDL_SCANCODE_V, MOD_CTRL },
	[POP_BTN_JOYSTICK_MODE] = { SDL_SCANCODE_J, MOD_CTRL },
	[POP_BTN_KEYBOARD_MODE] = { SDL_SCANCODE_K, MOD_CTRL },
	[POP_BTN_CHEAT_SHOW_ROOMS] = { SDL_SCANCODE_C, 0 },
	[POP_BTN_CHEAT_SHOW_CORNER_ROOMS] = { SDL_SCANCODE_C, MOD_SHIFT },
	[POP_BTN_CHEAT_LESS_TIME] = { SDL_SCANCODE_KP_MINUS, 0 },
	[POP_BTN_CHEAT_MORE_TIME] = { SDL_SCANCODE_KP_PLUS, 0 },
	[POP_BTN_CHEAT_REVIVE] = { SDL_SCANCODE_R, 0 },
	[POP_BTN_CHEAT_KILL_GUARD] = { SDL_SCANCODE_K, 0 },
	[POP_BTN_CHEAT_FLIP_SCREEN] = { SDL_SCANCODE_I, MOD_SHIFT },
	[POP_BTN_CHEAT_FEATHER_FALL] = { SDL_SCANCODE_W, MOD_SHIFT },
	[POP_BTN_CHEAT_LOOK_LEFT] = { SDL_SCANCODE_H, 0 },
	[POP_BTN_CHEAT_LOOK_RIGHT] = { SDL_SCANCODE_J, 0 },
	[POP_BTN_CHEAT_LOOK_UP] = { SDL_SCANCODE_U, 0 },
	[POP_BTN_CHEAT_LOOK_DOWN] = { SDL_SCANCODE_N, 0 },
	[POP_BTN_CHEAT_LOOK_BACK] = { SDL_SCANCODE_B, MOD_CTRL },
	[POP_BTN_CHEAT_BLIND_MODE] = { SDL_SCANCODE_B, MOD_SHIFT },
	[POP_BTN_CHEAT_ADD_HIT_POINT] = { SDL_SCANCODE_S, MOD_SHIFT },
	[POP_BTN_CHEAT_ADD_MAX_HIT_POINT] = { SDL_SCANCODE_T, MOD_SHIFT },
};

static void push_key(int down, SDL_Scancode sc)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = sc;
	e.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(sc);
	e.key.keysym.mod = (g.shift_down ? KMOD_LSHIFT : 0) | (g.ctrl_down ? KMOD_LCTRL : 0);
	g.keyboard[sc] = down ? 1 : 0;
	SDL_PushEvent(&e);
}

/* whether a modifier is still wanted by a held button */
static int mod_held(int mod)
{
	if (mod == MOD_SHIFT && g.held[POP_BTN_SHIFT]) return 1;
	for (int i = 0; i < POP_BTN_COUNT; i++)
		if (g.held[i] && (k_keys[i].mods & mod)) return 1;
	return 0;
}

/* The buttons that changed, as the key events a keyboard would have sent, in
 * the panel's order. The modifiers are real keys: a Ctrl or Shift command
 * puts its modifier down before its key and lifts it after, unless another
 * held button still holds it - so Shift and Next Level together are one Shift
 * key, and a key pressed while a modifier is down carries it. */
static void push_input(void)
{
	for (int i = 0; i < POP_BTN_COUNT; i++)
	{
		if (g.want[i] == g.held[i]) continue;
		const int down = g.want[i];
		g.held[i] = g.want[i];
		const int mods = i == POP_BTN_SHIFT ? MOD_SHIFT : k_keys[i].mods;
		if (down)
		{
			if ((mods & MOD_CTRL) && !g.ctrl_down) { g.ctrl_down = 1; push_key(1, SDL_SCANCODE_LCTRL); }
			if ((mods & MOD_SHIFT) && !g.shift_down) { g.shift_down = 1; push_key(1, SDL_SCANCODE_LSHIFT); }
			if (i != POP_BTN_SHIFT) push_key(1, k_keys[i].key);
		}
		else
		{
			if (i != POP_BTN_SHIFT) push_key(0, k_keys[i].key);
			if (g.ctrl_down && !mod_held(MOD_CTRL)) { g.ctrl_down = 0; push_key(0, SDL_SCANCODE_LCTRL); }
			if (g.shift_down && !mod_held(MOD_SHIFT)) { g.shift_down = 0; push_key(0, SDL_SCANCODE_LSHIFT); }
		}
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
