/* game-state.c - the game's properties, as docs/game-cores.md defines them.
 *
 * "Game State" is one packed block of the core's own memory: every scalar
 * below, at an offset assigned in table order. After each step the game's
 * variables are copied into it; before the next step the writable ones are
 * copied back - so a poke into the block is what the game finds when it
 * resumes, and a freeze (the same write before every step) holds.
 *
 * What SDLPoP keeps whole is exposed in place, as further domains, and
 * described in the same table: the level (its rooms' tiles, links and guards,
 * its start and its button events), the moving objects (falling floors), the
 * tile animations and the hall of fame. Those are the game's own memory, read
 * and written directly, and described the way they are laid out: tables as
 * arrays (count, stride), packed fields as bit fields, names as strings.
 *
 * The list follows JaffarPlus's SDLPoP game (games/sdlpop/princeOfPersia.hpp),
 * which is what a Prince of Persia TAS is searched with. Rooms are the game's
 * own numbers 1-24 (element i of a room table is room i+1) and tiles its own
 * positions 0-29, so Room 5.Tile[12] is the tile the game calls tile 12 of
 * room 5.
 */
#include "common.h"

#include "sdlpop-driver.h"

enum { T_U8, T_S8, T_U16, T_S16, T_U32, T_S32, T_U64, T_F32, T_BOOL };
static const char *const k_type_name[] = { "u8", "s8", "u16", "s16", "u32", "s32", "u64", "f32", "bool" };
static const int k_type_size[] = { 1, 1, 2, 2, 4, 4, 8, 4, 1 };

typedef struct
{
	const char *name;
	const char *group;
	int type;
	void *var;
	size_t var_size;
	int writable;
	const char *values; /* a JSON object, or NULL */
	const char *desc;
} prop;

/* derived each step (JaffarPlus computes the Kid's the same way) */
static uint8_t g_kid_prev_frame;
static float g_kid_pos_x, g_kid_pos_y;
static uint64_t g_core_clock;

/* seg001.c keeps the hall of fame's type to itself */
#pragma pack(push, 1)
typedef struct { char name[25]; short min, tick; } pop_hof_entry;
#pragma pack(pop)
extern pop_hof_entry hof[6];

#define DIR_VALUES "{\"-1\": \"Left\", \"0\": \"Right\", \"86\": \"None\"}"
#define ACTION_VALUES "{\"0\": \"Stand\", \"1\": \"Run jump\", \"2\": \"Hang climb\", \"3\": \"In midair\", \"4\": \"In freefall\", \"5\": \"Bumped\", \"6\": \"Hang straight\", \"7\": \"Turn\", \"99\": \"Hurt\"}"
#define CHARID_VALUES "{\"0\": \"Kid\", \"1\": \"Shadow\", \"2\": \"Guard\", \"3\": \"(unused)\", \"4\": \"Skeleton\", \"5\": \"Princess\", \"6\": \"Vizier\", \"24\": \"Mouse\"}"
#define SWORD_VALUES "{\"0\": \"Sheathed\", \"2\": \"Drawn\"}"

#define P(name, group, type, var, w, values, desc) { name, group, type, &(var), sizeof(var), w, values, desc }

static const prop k_props[] = {
	/* the prince */
	P("Kid.X", "Kid", T_U8, Kid.x, 1, NULL, "Horizontal position in the room, in game units (a tile is 14)"),
	P("Kid.Y", "Kid", T_U8, Kid.y, 1, NULL, "Vertical position: the floor line he stands on (55, 118 or 181 in the room's three rows)"),
	P("Kid.Room", "Kid", T_U8, Kid.room, 1, NULL, "The room he is in (1-24)"),
	P("Kid.Direction", "Kid", T_S8, Kid.direction, 1, DIR_VALUES, "The way he faces"),
	P("Kid.Frame", "Kid", T_U8, Kid.frame, 1, NULL, "Animation frame"),
	P("Kid.Action", "Kid", T_U8, Kid.action, 1, ACTION_VALUES, "What his current sequence is doing"),
	P("Kid.Column", "Kid", T_S8, Kid.curr_col, 1, NULL, "Tile column under him (0-9)"),
	P("Kid.Row", "Kid", T_S8, Kid.curr_row, 1, NULL, "Tile row he is on (0-2)"),
	P("Kid.Fall X", "Kid", T_S8, Kid.fall_x, 1, NULL, "Horizontal speed while falling or jumping"),
	P("Kid.Fall Y", "Kid", T_S8, Kid.fall_y, 1, NULL, "Falling speed, also how far he has fallen (under 22 is one row, under 33 two)"),
	P("Kid.Repeat", "Kid", T_U8, Kid.repeat, 1, NULL, "Repeat counter of his current sequence"),
	P("Kid.Char Id", "Kid", T_U8, Kid.charid, 1, CHARID_VALUES, "Which character this slot is"),
	P("Kid.Sword", "Kid", T_U8, Kid.sword, 1, SWORD_VALUES, "Whether his sword is drawn"),
	P("Kid.Alive", "Kid", T_S8, Kid.alive, 1, NULL, "Negative while alive; counts up after he dies"),
	P("Kid.Sequence", "Kid", T_U16, Kid.curr_seq, 1, NULL, "Position in the sequence table"),
	P("Kid.HP", "Kid", T_U16, hitp_curr, 1, NULL, "Hit points"),
	P("Kid.Max HP", "Kid", T_U16, hitp_max, 1, NULL, "Hit points he can have (the bottles on screen)"),
	P("Kid.HP At Level Start", "Kid", T_U16, hitp_beg_lev, 1, NULL, "Hit points a restarted level gives back"),
	P("Kid.Grab Timer", "Kid", T_U16, grab_timer, 1, NULL, "Ticks before he can grab a ledge again"),
	P("Kid.Holding Sword", "Kid", T_U16, holding_sword, 1, NULL, "Nonzero while he holds the sword"),
	P("Kid.Have Sword", "Kid", T_U16, have_sword, 1, NULL, "Nonzero once he has the sword"),
	P("Kid.Sword Strike", "Kid", T_U16, kid_sword_strike, 1, NULL, "Nonzero during his sword strike"),
	P("Kid.Pickup Object Type", "Kid", T_S16, pickup_obj_type, 1, NULL, "What he is picking up (potion type, or the sword)"),
	P("Kid.Off Guard", "Kid", T_U16, offguard, 1, NULL, "Nonzero when he put the sword away in a fight"),
	P("Kid.United With Shadow", "Kid", T_S16, united_with_shadow, 1, NULL, "Nonzero after he merges with his shadow (level 12)"),
	P("Kid.Previous Frame", "Kid", T_U8, g_kid_prev_frame, 0, NULL, "Kid.Frame one step earlier"),
	P("Kid.Pos X", "Kid", T_F32, g_kid_pos_x, 0, NULL, "Kid.X as a number (JaffarPlus's interpolated position)"),
	P("Kid.Pos Y", "Kid", T_F32, g_kid_pos_y, 0, NULL, "Kid.Y adjusted through climbing, hanging and jump-grab frames (JaffarPlus's interpolated position)"),

	/* the guard in the room (also the shadow, a skeleton, Jaffar) */
	P("Guard.X", "Guard", T_U8, Guard.x, 1, NULL, "Horizontal position in the room, in game units"),
	P("Guard.Y", "Guard", T_U8, Guard.y, 1, NULL, "Vertical position (a floor line)"),
	P("Guard.Room", "Guard", T_U8, Guard.room, 1, NULL, "The room he is in (1-24)"),
	P("Guard.Direction", "Guard", T_S8, Guard.direction, 1, DIR_VALUES, "The way he faces; None when there is no guard"),
	P("Guard.Frame", "Guard", T_U8, Guard.frame, 1, NULL, "Animation frame"),
	P("Guard.Action", "Guard", T_U8, Guard.action, 1, ACTION_VALUES, "What his current sequence is doing"),
	P("Guard.Column", "Guard", T_S8, Guard.curr_col, 1, NULL, "Tile column under him"),
	P("Guard.Row", "Guard", T_S8, Guard.curr_row, 1, NULL, "Tile row he is on"),
	P("Guard.Fall X", "Guard", T_S8, Guard.fall_x, 1, NULL, "Horizontal speed while falling"),
	P("Guard.Fall Y", "Guard", T_S8, Guard.fall_y, 1, NULL, "Falling speed"),
	P("Guard.Repeat", "Guard", T_U8, Guard.repeat, 1, NULL, "Repeat counter of his current sequence"),
	P("Guard.Char Id", "Guard", T_U8, Guard.charid, 1, CHARID_VALUES, "Which character he is"),
	P("Guard.Sword", "Guard", T_U8, Guard.sword, 1, SWORD_VALUES, "Whether his sword is drawn"),
	P("Guard.Alive", "Guard", T_S8, Guard.alive, 1, NULL, "Negative while alive"),
	P("Guard.Sequence", "Guard", T_U16, Guard.curr_seq, 1, NULL, "Position in the sequence table"),
	P("Guard.HP", "Guard", T_U16, guardhp_curr, 1, NULL, "Hit points"),
	P("Guard.Max HP", "Guard", T_U16, guardhp_max, 1, NULL, "Hit points he started with"),
	P("Guard.Color", "Guard", T_U16, curr_guard_color, 1, NULL, "Palette of his clothes"),
	P("Guard.Skill", "Guard", T_U16, guard_skill, 1, NULL, "Fighting skill (0-11)"),
	P("Guard.Notice Timer", "Guard", T_S16, guard_notice_timer, 1, NULL, "Ticks before he reacts to the prince"),
	P("Guard.Refrac", "Guard", T_U16, guard_refrac, 1, NULL, "Ticks before he can strike again"),
	P("Guard.Just Blocked", "Guard", T_U16, justblocked, 1, NULL, "Nonzero right after a block"),
	P("Guard.Dropped Out", "Guard", T_U16, droppedout, 1, NULL, "Nonzero once he has fallen out of the room"),
	P("Guard.Is Noticing", "Guard", T_U16, is_guard_notice, 1, NULL, "Nonzero while he notices the prince"),
	P("Guard.Can See Kid", "Guard", T_S16, can_guard_see_kid, 1, NULL, "0 cannot see him, 1 sees him, 2 sees him and nothing is in between"),
	P("Guard.Shadow Initialized", "Guard", T_U16, shadow_initialized, 1, NULL, "Nonzero once the shadow has been set up on this level"),

	/* the level being played */
	P("Level.Current", "Level", T_U16, current_level, 0, NULL, "The level being played (a poke does not load another; see Level.Next)"),
	P("Level.Next", "Level", T_U16, next_level, 1, NULL, "The level the game goes to when this tick ends; set it to leave the level"),
	P("Level.Checkpoint", "Level", T_U16, checkpoint, 1, NULL, "Nonzero once level 3's checkpoint is reached"),
	P("Level.Upside Down", "Level", T_U16, upside_down, 1, NULL, "Nonzero while the screen is upside down (the level 7 potion)"),
	P("Level.Drawn Room", "Level", T_U16, drawn_room, 1, NULL, "The room on screen"),
	P("Level.Door Open", "Level", T_U16, leveldoor_open, 1, NULL, "Nonzero once the exit door is open; 2 when it has been entered"),
	P("Level.Moving Objects", "Level", T_S16, mobs_count, 1, NULL, "How many Mob[] slots are in use"),
	P("Level.Tile Animations", "Level", T_S16, trobs_count, 1, NULL, "How many Trob[] slots are in use"),
	P("Level.Exit Room Timer", "Level", T_U16, exit_room_timer, 1, NULL, "Ticks before a room change can happen again"),
	P("Level.Jumped Through Mirror", "Level", T_S16, jumped_through_mirror, 1, NULL, "Level 4's mirror: -1 once the prince has jumped through it"),
	P("Level.Demo Index", "Level", T_U16, demo_index, 1, NULL, "Position in the demo level's recorded moves"),
	P("Level.Demo Time", "Level", T_S16, demo_time, 1, NULL, "Ticks left of the demo level's current move"),

	/* the clock of the game (not the core's) */
	P("Time.Minutes Left", "Time", T_S16, rem_min, 1, NULL, "Minutes left on the game's clock (negative: no time limit)"),
	P("Time.Ticks Left", "Time", T_U16, rem_tick, 1, NULL, "Ticks left in the current minute, 12 a second (719 down to 0)"),

	P("Time.Core Clock", "Time", T_U64, g_core_clock, 0, NULL, "The core's clock since power-on, 1,764,000 counts a second (what the steps are measured in)"),

	P("Random Seed", "Random", T_U32, random_seed, 1, NULL, "The game's random number generator (seed = seed * 214013 + 2531011 at each draw)"),

	P("Sound.Need Level 1 Music", "Sound", T_U16, need_level1_music, 1, NULL, "Nonzero until level 1's opening music has played"),
	P("Sound.Is Screaming", "Sound", T_U16, is_screaming, 1, NULL, "Nonzero while the falling scream plays"),
	P("Sound.Last Loose Sound", "Sound", T_U16, last_loose_sound, 1, NULL, "Which of the loose floor sounds played last"),

	P("Effects.Flash Color", "Effects", T_U16, flash_color, 1, NULL, "Color of the screen flash"),
	P("Effects.Flash Time", "Effects", T_U16, flash_time, 1, NULL, "Ticks of screen flash left"),
	P("Effects.Feather Fall", "Effects", T_U16, is_feather_fall, 1, NULL, "Ticks of feather fall left (the green potion)"),

	P("Collision.Row", "Collision", T_S8, collision_row, 1, NULL, "Row the last collision was checked on"),
	P("Collision.Previous Row", "Collision", T_S8, prev_collision_row, 1, NULL, "Row the collision before it was checked on"),

	P("Hall of Fame.Entries", "Hall of Fame", T_S16, hof_count, 1, NULL, "How many Hall of Fame entries are filled"),
};
#define PROP_COUNT ((int)(sizeof k_props / sizeof k_props[0]))

#define MOB_COUNT ((int)(sizeof mobs / sizeof mobs[0]))
#define TROB_COUNT ((int)(sizeof trobs / sizeof trobs[0]))

static int g_offset[PROP_COUNT];
static int g_block_size;
static uint8_t g_block[512];
static char *g_json;

/* ------------------------------------------------------------ the domains */

int popdrv_domain_count(void) { return 6; }

const char *popdrv_domain_name(int i)
{
	static const char *const names[] = { "Game State", "Level", "Mobs", "Trobs", "Hall of Fame", "Custom Options" };
	return i >= 0 && i < 6 ? names[i] : NULL;
}

uint8_t *popdrv_domain_ptr(int i)
{
	switch (i)
	{
	case 0: return g_block;
	case 1: return (uint8_t *)&level;
	case 2: return (uint8_t *)mobs;
	case 3: return (uint8_t *)trobs;
	case 4: return (uint8_t *)hof;
	/* the options the game plays by (SDLPoP's custom options, which the core
	 * always uses: the release's guard tables and level colours are there) */
	case 5: return (uint8_t *)custom;
	default: return NULL;
	}
}

int64_t popdrv_domain_size(int i)
{
	switch (i)
	{
	case 0: return g_block_size;
	case 1: return (int64_t)sizeof level;
	case 2: return (int64_t)sizeof mobs;
	case 3: return (int64_t)sizeof trobs;
	case 4: return (int64_t)sizeof(pop_hof_entry) * 6;
	case 5: return (int64_t)sizeof(custom_options_type);
	default: return 0;
	}
}

const char *popdrv_game_properties(void) { return g_json ? g_json : ""; }

/* ------------------------------------------------------------ the copies */

void gamestate_from_game(void)
{
	/* JaffarPlus's interpolation, arithmetic for arithmetic (its frame
	 * difference is a byte, so "went down" is never true) */
	uint8_t diff = (uint8_t)(Kid.frame - g_kid_prev_frame);
	float x = (float)Kid.x, y = (float)Kid.y;
	if (Kid.frame >= 0x8D && Kid.frame <= 0x94 && diff > 0) y += 7.0f - (Kid.frame - 0x8D);
	if (Kid.frame >= 0x43 && Kid.frame <= 0x4F) y -= 16.0f - (0x4F - Kid.frame);
	if (Kid.frame == 0x50) y -= 20.0f;
	if (Kid.frame >= 0x57 && Kid.frame <= 0x5B) y -= 25.0f - (0x5B - Kid.frame);
	if (Kid.frame >= 0x87 && Kid.frame < 0x8D) y -= 32.0f + 7.0f - (0x8D - Kid.frame);
	g_kid_pos_x = x;
	g_kid_pos_y = y;
	g_core_clock = popdrv_clock();

	for (int i = 0; i < PROP_COUNT; i++)
		memcpy(g_block + g_offset[i], k_props[i].var, k_props[i].var_size);

	/* for the next step's copy */
	g_kid_prev_frame = Kid.frame;
}

void gamestate_to_game(void)
{
	for (int i = 0; i < PROP_COUNT; i++)
		if (k_props[i].writable)
			memcpy(k_props[i].var, g_block + g_offset[i], k_props[i].var_size);
}

/* ------------------------------------------------------------ the table */

typedef struct { char *p; size_t len, cap; } sbuf;

static void sb_add(sbuf *b, const char *fmt, ...)
{
	for (;;)
	{
		va_list ap;
		va_start(ap, fmt);
		int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
		va_end(ap);
		if (n >= 0 && b->len + (size_t)n < b->cap) { b->len += (size_t)n; return; }
		b->cap = b->cap * 2 + (size_t)(n > 0 ? n : 0) + 64;
		b->p = realloc(b->p, b->cap);
	}
}

typedef struct
{
	const char *name, *domain, *group;
	size_t offset;
	int type;
	int count, stride;   /* an array: count elements, stride bytes apart (0: one value) */
	int bit, bits;       /* a bit field (bits 0: the whole value) */
	int length;          /* a string of this many bytes (type unused) */
	int writable;
	const char *values, *desc;
} entry_t;

static void entry(sbuf *b, const entry_t *e)
{
	sb_add(b, "%s\n    { \"name\": \"%s\", \"domain\": \"%s\", \"offset\": %zu, ", b->len > 20 ? "," : "",
	       e->name, e->domain, e->offset);
	if (e->length) sb_add(b, "\"type\": \"string\", \"length\": %d, \"encoding\": \"ascii\"", e->length);
	else sb_add(b, "\"type\": \"%s\"", k_type_name[e->type]);
	if (e->count) sb_add(b, ", \"count\": %d, \"stride\": %d", e->count, e->stride);
	if (e->bits) sb_add(b, ", \"bit\": %d, \"bits\": %d", e->bit, e->bits);
	sb_add(b, ", \"group\": \"%s\"", e->group);
	if (!e->writable) sb_add(b, ", \"writable\": false");
	if (e->values) sb_add(b, ", \"values\": %s", e->values);
	if (e->desc) sb_add(b, ", \"description\": \"%s\"", e->desc);
	sb_add(b, " }");
}

#define TILE_VALUES "{\"0\": \"Empty\", \"1\": \"Floor\", \"2\": \"Spikes\", \"3\": \"Pillar\", \"4\": \"Gate\", \"5\": \"Stuck button\", \"6\": \"Drop button\", \"7\": \"Tapestry\", \"8\": \"Big pillar bottom\", \"9\": \"Big pillar top\", \"10\": \"Potion\", \"11\": \"Loose floor\", \"12\": \"Tapestry top\", \"13\": \"Mirror\", \"14\": \"Debris\", \"15\": \"Raise button\", \"16\": \"Exit door left\", \"17\": \"Exit door right\", \"18\": \"Chomper\", \"19\": \"Torch\", \"20\": \"Wall\", \"21\": \"Skeleton\", \"22\": \"Sword\", \"23\": \"Balcony left\", \"24\": \"Balcony right\", \"25\": \"Lattice pillar\", \"26\": \"Lattice down\", \"27\": \"Lattice small\", \"28\": \"Lattice left\", \"29\": \"Lattice right\", \"30\": \"Torch with debris\"}"

int gamestate_init(char *err, int errsize)
{
	int off = 0;
	for (int i = 0; i < PROP_COUNT; i++)
	{
		if ((size_t)k_type_size[k_props[i].type] != k_props[i].var_size)
		{
			snprintf(err, (size_t)errsize, "property %s: a %s over a %zu-byte variable", k_props[i].name,
			         k_type_name[k_props[i].type], k_props[i].var_size);
			return 0;
		}
		g_offset[i] = off;
		off += (int)k_props[i].var_size;
	}
	if (off > (int)sizeof g_block)
	{
		snprintf(err, (size_t)errsize, "the property block needs %d bytes", off);
		return 0;
	}
	g_block_size = off;

	sbuf b = { malloc(1 << 16), 0, 1 << 16 };
	sb_add(&b, "{\n  \"properties\": [");
	for (int i = 0; i < PROP_COUNT; i++)
	{
		entry_t e = { k_props[i].name, "Game State", k_props[i].group, (size_t)g_offset[i], k_props[i].type,
		              0, 0, 0, 0, 0, k_props[i].writable, k_props[i].values, k_props[i].desc };
		entry(&b, &e);
	}

	/* the level, in place */
	{
		entry_t e[] = {
			{ "Level.Start Room", "Level", "Level", offsetof(level_type, start_room), T_U8, 0, 0, 0, 0, 0, 1, NULL, "The room the level starts in" },
			{ "Level.Start Position", "Level", "Level", offsetof(level_type, start_pos), T_U8, 0, 0, 0, 0, 0, 1, NULL, "The tile the level starts on (0-29)" },
			{ "Level.Start Direction", "Level", "Level", offsetof(level_type, start_dir), T_S8, 0, 0, 0, 0, 0, 1, DIR_VALUES, "The way the prince faces at the start" },
			{ "Level.Used Rooms", "Level", "Level", offsetof(level_type, used_rooms), T_U8, 0, 0, 0, 0, 0, 1, NULL, "How many rooms the level has" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}
	for (int r = 1; r <= 24; r++)
	{
		/* a room's 30 tiles: the type in the low five bits of the foreground
		 * byte, a flag above it, and the modifier (the background byte) */
		char n1[40], n2[40], n3[40];
		snprintf(n1, sizeof n1, "Room %d.Tile", r);
		snprintf(n2, sizeof n2, "Room %d.Tile Flag", r);
		snprintf(n3, sizeof n3, "Room %d.Modifier", r);
		const size_t at = (size_t)(r - 1) * 30;
		entry_t e[] = {
			{ n1, "Level", "Tiles", offsetof(level_type, fg) + at, T_U8, 30, 1, 0, 5, 0, 1, TILE_VALUES,
			  r == 1 ? "The tile, element i being tile i (0-9 the top row, 10-19, 20-29)" : NULL },
			{ n2, "Level", "Tiles", offsetof(level_type, fg) + at, T_U8, 30, 1, 5, 1, 0, 1, NULL,
			  r == 1 ? "Bit 5 of the tile byte: on a loose floor, one that does not fall" : NULL },
			{ n3, "Level", "Tiles", offsetof(level_type, bg) + at, T_U8, 30, 1, 0, 0, 0, 1, NULL,
			  r == 1 ? "The tile's modifier: a gate's openness, a potion's kind, a button's first event, a chomper's phase" : NULL },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}
	{
		const size_t links = offsetof(level_type, roomlinks);
		const int ls = (int)sizeof(link_type);
		entry_t e[] = {
			{ "Room Links.Left", "Level", "Rooms", links + offsetof(link_type, left), T_U8, 24, ls, 0, 0, 0, 1, NULL, "The room beyond the left edge of room i+1 (0: none)" },
			{ "Room Links.Right", "Level", "Rooms", links + offsetof(link_type, right), T_U8, 24, ls, 0, 0, 0, 1, NULL, "The room beyond the right edge of room i+1 (0: none)" },
			{ "Room Links.Up", "Level", "Rooms", links + offsetof(link_type, up), T_U8, 24, ls, 0, 0, 0, 1, NULL, "The room above room i+1 (0: none)" },
			{ "Room Links.Down", "Level", "Rooms", links + offsetof(link_type, down), T_U8, 24, ls, 0, 0, 0, 1, NULL, "The room below room i+1 (0: none)" },
			{ "Room Guards.Tile", "Level", "Rooms", offsetof(level_type, guards_tile), T_U8, 24, 1, 0, 0, 0, 1, NULL, "The tile room i+1's guard starts on (30 or more: no guard)" },
			{ "Room Guards.Direction", "Level", "Rooms", offsetof(level_type, guards_dir), T_U8, 24, 1, 0, 0, 0, 1, "{\"0\": \"Right\", \"255\": \"Left\"}", "The way room i+1's guard faces" },
			{ "Room Guards.X", "Level", "Rooms", offsetof(level_type, guards_x), T_U8, 24, 1, 0, 0, 0, 1, NULL, "Room i+1's guard's position within his tile" },
			{ "Room Guards.Sequence Low", "Level", "Rooms", offsetof(level_type, guards_seq_lo), T_U8, 24, 1, 0, 0, 0, 1, NULL, "Room i+1's guard's sequence, low byte (a guard remembered in another room)" },
			{ "Room Guards.Skill", "Level", "Rooms", offsetof(level_type, guards_skill), T_U8, 24, 1, 0, 0, 0, 1, NULL, "Room i+1's guard's fighting skill" },
			{ "Room Guards.Sequence High", "Level", "Rooms", offsetof(level_type, guards_seq_hi), T_U8, 24, 1, 0, 0, 0, 1, NULL, "Room i+1's guard's sequence, high byte" },
			{ "Room Guards.Color", "Level", "Rooms", offsetof(level_type, guards_color), T_U8, 24, 1, 0, 0, 0, 1, NULL, "Room i+1's guard's clothes" },
			/* the 256 button events, packed as SDLPoP's get_doorlink_* unpack them */
			{ "Events.Tile", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 0, 5, 0, 1, NULL, "The tile (0-29) of the door event i opens or closes" },
			{ "Events.Room Low", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 5, 2, 0, 1, NULL, "The door's room, low two bits (room = Room Low + 4 x Room High)" },
			{ "Events.Last", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 7, 1, 0, 1, NULL, "1 when event i is the last its button triggers" },
			{ "Events.Timer", "Level", "Events", offsetof(level_type, doorlinks2), T_U8, 256, 1, 0, 5, 0, 1, NULL, "Event i's timer (a jammed button's is 0x1F)" },
			{ "Events.Room High", "Level", "Events", offsetof(level_type, doorlinks2), T_U8, 256, 1, 5, 3, 0, 1, NULL, "The door's room, high three bits" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}

	/* the moving objects, the tile animations and the hall of fame, in place */
	{
		const int ms = (int)sizeof(mob_type), ts = (int)sizeof(trob_type), hs = (int)sizeof(pop_hof_entry);
		entry_t e[] = {
			{ "Mobs.X", "Mobs", "Moving Objects", offsetof(mob_type, xh), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, "A falling loose floor's horizontal position (the first Level.Moving Objects are in use)" },
			{ "Mobs.Y", "Mobs", "Moving Objects", offsetof(mob_type, y), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Room", "Mobs", "Moving Objects", offsetof(mob_type, room), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Speed", "Mobs", "Moving Objects", offsetof(mob_type, speed), T_S8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Type", "Mobs", "Moving Objects", offsetof(mob_type, type), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Row", "Mobs", "Moving Objects", offsetof(mob_type, row), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Trobs.Tile", "Trobs", "Tile Animations", offsetof(trob_type, tilepos), T_U8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, "An animating tile - a gate, a button, a chomper, a potion (the first Level.Tile Animations are in use)" },
			{ "Trobs.Room", "Trobs", "Tile Animations", offsetof(trob_type, room), T_U8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, NULL },
			{ "Trobs.Type", "Trobs", "Tile Animations", offsetof(trob_type, type), T_S8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, NULL },
			{ "Hall of Fame.Name", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, name), T_U8, 6, hs, 0, 0, 25, 1, NULL, "The names entered after winning, best time first" },
			{ "Hall of Fame.Minutes", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, min), T_S16, 6, hs, 0, 0, 0, 1, NULL, "Minutes that were left" },
			{ "Hall of Fame.Ticks", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, tick), T_S16, 6, hs, 0, 0, 0, 1, NULL, "Ticks that were left in the minute" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}

	/* the release's own tables the game plays by: how each guard skill (0-11)
	 * fights - 1.0's, or the later ones of 1.1, 1.3 and 1.4 - and the level
	 * colours 1.3 and 1.4 have */
	{
		const int w = 2;
		entry_t e[] = {
			{ "Guard Skills.Strike Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, strikeprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "Out of 255: how likely a guard of skill i is to strike" },
			{ "Guard Skills.Restrike Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, restrikeprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "Out of 255: how likely he is to strike again after a blocked strike" },
			{ "Guard Skills.Block Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, blockprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "Out of 255: how likely he is to block a strike" },
			{ "Guard Skills.Impaired Block Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, impblockprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "Out of 255: how likely he is to block while impaired" },
			{ "Guard Skills.Advance Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, advprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "Out of 255: how likely he is to advance" },
			{ "Guard Skills.Refractory Timer", "Custom Options", "Guard Skills", offsetof(custom_options_type, refractimer), T_U16, 12, w, 0, 0, 0, 1, NULL, "Ticks he waits after a strike" },
			{ "Guard Skills.Extra Strength", "Custom Options", "Guard Skills", offsetof(custom_options_type, extrastrength), T_U16, 12, w, 0, 0, 0, 1, NULL, "Hit points he has beyond his level's" },
			{ "Level Colours", "Custom Options", "Levels", offsetof(custom_options_type, tbl_level_color), T_U16, 16, w, 0, 0, 0, 1, NULL, "Level i's colour variation (0: none; 1.3 and 1.4 only)" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}
	sb_add(&b, "\n  ]\n}\n");
	g_json = b.p;
	return 1;
}
