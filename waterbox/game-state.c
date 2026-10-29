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

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). */
#define TEXT_ARG "\x1f"

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
static uint32_t g_igt_ticks, g_igt_ms;

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
	P("Kid.X", "Kid", T_U8, Kid.x, 1, NULL, "property.kid_x"),
	P("Kid.Y", "Kid", T_U8, Kid.y, 1, NULL, "property.kid_y"),
	P("Kid.Room", "Kid", T_U8, Kid.room, 1, NULL, "property.kid_room"),
	P("Kid.Direction", "Kid", T_S8, Kid.direction, 1, DIR_VALUES, "property.kid_direction"),
	P("Kid.Frame", "Kid", T_U8, Kid.frame, 1, NULL, "property.kid_frame"),
	P("Kid.Action", "Kid", T_U8, Kid.action, 1, ACTION_VALUES, "property.kid_action"),
	P("Kid.Column", "Kid", T_S8, Kid.curr_col, 1, NULL, "property.kid_column"),
	P("Kid.Row", "Kid", T_S8, Kid.curr_row, 1, NULL, "property.kid_row"),
	P("Kid.Fall X", "Kid", T_S8, Kid.fall_x, 1, NULL, "property.kid_fall_x"),
	P("Kid.Fall Y", "Kid", T_S8, Kid.fall_y, 1, NULL, "property.kid_fall_y"),
	P("Kid.Repeat", "Kid", T_U8, Kid.repeat, 1, NULL, "property.kid_repeat"),
	P("Kid.Char Id", "Kid", T_U8, Kid.charid, 1, CHARID_VALUES, "property.kid_char_id"),
	P("Kid.Sword", "Kid", T_U8, Kid.sword, 1, SWORD_VALUES, "property.kid_sword"),
	P("Kid.Alive", "Kid", T_S8, Kid.alive, 1, NULL, "property.kid_alive"),
	P("Kid.Sequence", "Kid", T_U16, Kid.curr_seq, 1, NULL, "property.kid_sequence"),
	P("Kid.HP", "Kid", T_U16, hitp_curr, 1, NULL, "setting.start_hitp"),
	P("Kid.Max HP", "Kid", T_U16, hitp_max, 1, NULL, "property.kid_max_hp"),
	P("Kid.HP At Level Start", "Kid", T_U16, hitp_beg_lev, 1, NULL, "property.kid_hp_at_level_start"),
	P("Kid.Grab Timer", "Kid", T_U16, grab_timer, 1, NULL, "property.kid_grab_timer"),
	P("Kid.Holding Sword", "Kid", T_U16, holding_sword, 1, NULL, "property.kid_holding_sword"),
	P("Kid.Have Sword", "Kid", T_U16, have_sword, 1, NULL, "property.kid_have_sword"),
	P("Kid.Sword Strike", "Kid", T_U16, kid_sword_strike, 1, NULL, "property.kid_sword_strike"),
	P("Kid.Pickup Object Type", "Kid", T_S16, pickup_obj_type, 1, NULL, "property.kid_pickup_object_type"),
	P("Kid.Off Guard", "Kid", T_U16, offguard, 1, NULL, "property.kid_off_guard"),
	P("Kid.United With Shadow", "Kid", T_S16, united_with_shadow, 1, NULL, "property.kid_united_with_shadow"),
	P("Kid.Previous Frame", "Kid", T_U8, g_kid_prev_frame, 0, NULL, "property.kid_previous_frame"),
	P("Kid.Pos X", "Kid", T_F32, g_kid_pos_x, 0, NULL, "property.kid_pos_x"),
	P("Kid.Pos Y", "Kid", T_F32, g_kid_pos_y, 0, NULL, "property.kid_pos_y"),

	/* the guard in the room (also the shadow, a skeleton, Jaffar) */
	P("Guard.X", "Guard", T_U8, Guard.x, 1, NULL, "property.guard_x"),
	P("Guard.Y", "Guard", T_U8, Guard.y, 1, NULL, "property.guard_y"),
	P("Guard.Room", "Guard", T_U8, Guard.room, 1, NULL, "property.kid_room"),
	P("Guard.Direction", "Guard", T_S8, Guard.direction, 1, DIR_VALUES, "property.guard_direction"),
	P("Guard.Frame", "Guard", T_U8, Guard.frame, 1, NULL, "property.kid_frame"),
	P("Guard.Action", "Guard", T_U8, Guard.action, 1, ACTION_VALUES, "property.kid_action"),
	P("Guard.Column", "Guard", T_S8, Guard.curr_col, 1, NULL, "property.guard_column"),
	P("Guard.Row", "Guard", T_S8, Guard.curr_row, 1, NULL, "property.guard_row"),
	P("Guard.Fall X", "Guard", T_S8, Guard.fall_x, 1, NULL, "property.guard_fall_x"),
	P("Guard.Fall Y", "Guard", T_S8, Guard.fall_y, 1, NULL, "property.guard_fall_y"),
	P("Guard.Repeat", "Guard", T_U8, Guard.repeat, 1, NULL, "property.kid_repeat"),
	P("Guard.Char Id", "Guard", T_U8, Guard.charid, 1, CHARID_VALUES, "property.guard_char_id"),
	P("Guard.Sword", "Guard", T_U8, Guard.sword, 1, SWORD_VALUES, "property.kid_sword"),
	P("Guard.Alive", "Guard", T_S8, Guard.alive, 1, NULL, "property.guard_alive"),
	P("Guard.Sequence", "Guard", T_U16, Guard.curr_seq, 1, NULL, "property.kid_sequence"),
	P("Guard.HP", "Guard", T_U16, guardhp_curr, 1, NULL, "setting.start_hitp"),
	P("Guard.Max HP", "Guard", T_U16, guardhp_max, 1, NULL, "property.guard_max_hp"),
	P("Guard.Color", "Guard", T_U16, curr_guard_color, 1, NULL, "property.guard_color"),
	P("Guard.Skill", "Guard", T_U16, guard_skill, 1, NULL, "property.guard_skill"),
	P("Guard.Notice Timer", "Guard", T_S16, guard_notice_timer, 1, NULL, "property.guard_notice_timer"),
	P("Guard.Refrac", "Guard", T_U16, guard_refrac, 1, NULL, "property.guard_refrac"),
	P("Guard.Just Blocked", "Guard", T_U16, justblocked, 1, NULL, "property.guard_just_blocked"),
	P("Guard.Dropped Out", "Guard", T_U16, droppedout, 1, NULL, "property.guard_dropped_out"),
	P("Guard.Is Noticing", "Guard", T_U16, is_guard_notice, 1, NULL, "property.guard_is_noticing"),
	P("Guard.Can See Kid", "Guard", T_S16, can_guard_see_kid, 1, NULL, "property.guard_can_see_kid"),
	P("Guard.Shadow Initialized", "Guard", T_U16, shadow_initialized, 1, NULL, "property.guard_shadow_initialized"),

	/* the level being played */
	P("Level.Current", "Level", T_U16, current_level, 0, NULL, "property.level_current"),
	P("Level.Next", "Level", T_U16, next_level, 1, NULL, "property.level_next"),
	P("Level.Checkpoint", "Level", T_U16, checkpoint, 1, NULL, "property.level_checkpoint"),
	P("Level.Upside Down", "Level", T_U16, upside_down, 1, NULL, "property.level_upside_down"),
	P("Level.Drawn Room", "Level", T_U16, drawn_room, 1, NULL, "property.level_drawn_room"),
	P("Level.Door Open", "Level", T_U16, leveldoor_open, 1, NULL, "property.level_door_open"),
	P("Level.Moving Objects", "Level", T_S16, mobs_count, 1, NULL, "property.level_moving_objects"),
	P("Level.Tile Animations", "Level", T_S16, trobs_count, 1, NULL, "property.level_tile_animations"),
	P("Level.Exit Room Timer", "Level", T_U16, exit_room_timer, 1, NULL, "property.level_exit_room_timer"),
	P("Level.Jumped Through Mirror", "Level", T_S16, jumped_through_mirror, 1, NULL, "property.level_jumped_through_mirror"),
	P("Level.Demo Index", "Level", T_U16, demo_index, 1, NULL, "property.level_demo_index"),
	P("Level.Demo Time", "Level", T_S16, demo_time, 1, NULL, "property.level_demo_time"),

	/* the clock of the game (not the core's) */
	P("Time.Minutes Left", "Time", T_S16, rem_min, 1, NULL, "property.time_minutes_left"),
	P("Time.Ticks Left", "Time", T_U16, rem_tick, 1, NULL, "property.time_ticks_left"),

	P("Time.IGT Ticks", "Time", T_U32, g_igt_ticks, 0, NULL, "property.time_igt_ticks"),
	P("Time.IGT Ms", "Time", T_U32, g_igt_ms, 0, NULL, "property.time_igt_ms"),
	P("Time.Core Clock", "Time", T_U64, g_core_clock, 0, NULL, "property.time_core_clock"),

	P("Random Seed", "Random", T_U32, random_seed, 1, NULL, "property.random_seed"),

	P("Sound.Need Level 1 Music", "Sound", T_U16, need_level1_music, 1, NULL, "property.sound_need_level_1_music"),
	P("Sound.Is Screaming", "Sound", T_U16, is_screaming, 1, NULL, "property.sound_is_screaming"),
	P("Sound.Last Loose Sound", "Sound", T_U16, last_loose_sound, 1, NULL, "property.sound_last_loose_sound"),

	P("Effects.Flash Color", "Effects", T_U16, flash_color, 1, NULL, "property.effects_flash_color"),
	P("Effects.Flash Time", "Effects", T_U16, flash_time, 1, NULL, "property.effects_flash_time"),
	P("Effects.Feather Fall", "Effects", T_U16, is_feather_fall, 1, NULL, "property.effects_feather_fall"),

	P("Collision.Row", "Collision", T_S8, collision_row, 1, NULL, "property.collision_row"),
	P("Collision.Previous Row", "Collision", T_S8, prev_collision_row, 1, NULL, "property.collision_previous_row"),

	P("Hall of Fame.Entries", "Hall of Fame", T_S16, hof_count, 1, NULL, "property.hall_of_fame_entries"),
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

	/* The in-game time (TASVideos' IGT): what the game's clock has counted down
	 * from the time a game starts with. show_time() takes a tick off each tick
	 * of play and, at 0, puts 719 back and takes a minute: so a minute is 719
	 * ticks and the count is minutes x 719 + ticks. Before any game the clock
	 * holds nothing, and neither does the time. */
	{
		const int64_t start = (int64_t)custom->start_minutes_left * 719 + custom->start_ticks_left;
		const int64_t now = (int64_t)rem_min * 719 + rem_tick;
		const int64_t ticks = rem_min == 0 && rem_tick == 0 ? 0 : start - now;
		g_igt_ticks = ticks > 0 ? (uint32_t)ticks : 0;
		g_igt_ms = (uint32_t)((uint64_t)g_igt_ticks * 1000 / 12);
	}

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

typedef struct { char *p; size_t len, cap, list; } sbuf;   /* list: where the properties list begins */

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
	sb_add(b, "%s\n    { \"name\": \"%s\", \"domain\": \"%s\", \"offset\": %zu, ", b->len > b->list ? "," : "",
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
		snprintf(err, (size_t)errsize, "error.property_block" TEXT_ARG "%d", off);
		return 0;
	}
	g_block_size = off;

	sbuf b = { malloc(1 << 16), 0, 1 << 16 };
	/* the game's own timer, for the frontend (docs/game-cores.md) */
	sb_add(&b, "{\n  \"gameTimer\": \"Time.IGT Ms\",\n  \"properties\": [");
	b.list = b.len;
	for (int i = 0; i < PROP_COUNT; i++)
	{
		entry_t e = { k_props[i].name, "Game State", k_props[i].group, (size_t)g_offset[i], k_props[i].type,
		              0, 0, 0, 0, 0, k_props[i].writable, k_props[i].values, k_props[i].desc };
		entry(&b, &e);
	}

	/* the level, in place */
	{
		entry_t e[] = {
			{ "Level.Start Room", "Level", "Level", offsetof(level_type, start_room), T_U8, 0, 0, 0, 0, 0, 1, NULL, "property.level_start_room" },
			{ "Level.Start Position", "Level", "Level", offsetof(level_type, start_pos), T_U8, 0, 0, 0, 0, 0, 1, NULL, "property.level_start_position" },
			{ "Level.Start Direction", "Level", "Level", offsetof(level_type, start_dir), T_S8, 0, 0, 0, 0, 0, 1, DIR_VALUES, "property.level_start_direction" },
			{ "Level.Used Rooms", "Level", "Level", offsetof(level_type, used_rooms), T_U8, 0, 0, 0, 0, 0, 1, NULL, "property.level_used_rooms" },
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
			  r == 1 ? "property.room_tile" : NULL },
			{ n2, "Level", "Tiles", offsetof(level_type, fg) + at, T_U8, 30, 1, 5, 1, 0, 1, NULL,
			  r == 1 ? "property.room_tile_flag" : NULL },
			{ n3, "Level", "Tiles", offsetof(level_type, bg) + at, T_U8, 30, 1, 0, 0, 0, 1, NULL,
			  r == 1 ? "property.room_modifier" : NULL },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}
	{
		const size_t links = offsetof(level_type, roomlinks);
		const int ls = (int)sizeof(link_type);
		entry_t e[] = {
			{ "Room Links.Left", "Level", "Rooms", links + offsetof(link_type, left), T_U8, 24, ls, 0, 0, 0, 1, NULL, "property.room_links_left" },
			{ "Room Links.Right", "Level", "Rooms", links + offsetof(link_type, right), T_U8, 24, ls, 0, 0, 0, 1, NULL, "property.room_links_right" },
			{ "Room Links.Up", "Level", "Rooms", links + offsetof(link_type, up), T_U8, 24, ls, 0, 0, 0, 1, NULL, "property.room_links_up" },
			{ "Room Links.Down", "Level", "Rooms", links + offsetof(link_type, down), T_U8, 24, ls, 0, 0, 0, 1, NULL, "property.room_links_down" },
			{ "Room Guards.Tile", "Level", "Rooms", offsetof(level_type, guards_tile), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_tile" },
			{ "Room Guards.Direction", "Level", "Rooms", offsetof(level_type, guards_dir), T_U8, 24, 1, 0, 0, 0, 1, "{\"0\": \"Right\", \"255\": \"Left\"}", "property.room_guards_direction" },
			{ "Room Guards.X", "Level", "Rooms", offsetof(level_type, guards_x), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_x" },
			{ "Room Guards.Sequence Low", "Level", "Rooms", offsetof(level_type, guards_seq_lo), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_sequence_low" },
			{ "Room Guards.Skill", "Level", "Rooms", offsetof(level_type, guards_skill), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_skill" },
			{ "Room Guards.Sequence High", "Level", "Rooms", offsetof(level_type, guards_seq_hi), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_sequence_high" },
			{ "Room Guards.Color", "Level", "Rooms", offsetof(level_type, guards_color), T_U8, 24, 1, 0, 0, 0, 1, NULL, "property.room_guards_color" },
			/* the 256 button events, packed as SDLPoP's get_doorlink_* unpack them */
			{ "Events.Tile", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 0, 5, 0, 1, NULL, "property.events_tile" },
			{ "Events.Room Low", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 5, 2, 0, 1, NULL, "property.events_room_low" },
			{ "Events.Last", "Level", "Events", offsetof(level_type, doorlinks1), T_U8, 256, 1, 7, 1, 0, 1, NULL, "property.events_last" },
			{ "Events.Timer", "Level", "Events", offsetof(level_type, doorlinks2), T_U8, 256, 1, 0, 5, 0, 1, NULL, "property.events_timer" },
			{ "Events.Room High", "Level", "Events", offsetof(level_type, doorlinks2), T_U8, 256, 1, 5, 3, 0, 1, NULL, "property.events_room_high" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}

	/* the moving objects, the tile animations and the hall of fame, in place */
	{
		const int ms = (int)sizeof(mob_type), ts = (int)sizeof(trob_type), hs = (int)sizeof(pop_hof_entry);
		entry_t e[] = {
			{ "Mobs.X", "Mobs", "Moving Objects", offsetof(mob_type, xh), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, "property.mobs_x" },
			{ "Mobs.Y", "Mobs", "Moving Objects", offsetof(mob_type, y), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Room", "Mobs", "Moving Objects", offsetof(mob_type, room), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Speed", "Mobs", "Moving Objects", offsetof(mob_type, speed), T_S8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Type", "Mobs", "Moving Objects", offsetof(mob_type, type), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Mobs.Row", "Mobs", "Moving Objects", offsetof(mob_type, row), T_U8, MOB_COUNT, ms, 0, 0, 0, 1, NULL, NULL },
			{ "Trobs.Tile", "Trobs", "Tile Animations", offsetof(trob_type, tilepos), T_U8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, "property.trobs_tile" },
			{ "Trobs.Room", "Trobs", "Tile Animations", offsetof(trob_type, room), T_U8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, NULL },
			{ "Trobs.Type", "Trobs", "Tile Animations", offsetof(trob_type, type), T_S8, TROB_COUNT, ts, 0, 0, 0, 1, NULL, NULL },
			{ "Hall of Fame.Name", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, name), T_U8, 6, hs, 0, 0, 25, 1, NULL, "property.hall_of_fame_name" },
			{ "Hall of Fame.Minutes", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, min), T_S16, 6, hs, 0, 0, 0, 1, NULL, "property.hall_of_fame_minutes" },
			{ "Hall of Fame.Ticks", "Hall of Fame", "Hall of Fame", offsetof(pop_hof_entry, tick), T_S16, 6, hs, 0, 0, 0, 1, NULL, "property.hall_of_fame_ticks" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}

	/* the release's own tables the game plays by: how each guard skill (0-11)
	 * fights - 1.0's, or the later ones of 1.1, 1.3 and 1.4 - and the level
	 * colours 1.3 and 1.4 have */
	{
		const int w = 2;
		entry_t e[] = {
			{ "Guard Skills.Strike Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, strikeprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_strike_probability" },
			{ "Guard Skills.Restrike Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, restrikeprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_restrike_probability" },
			{ "Guard Skills.Block Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, blockprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_block_probability" },
			{ "Guard Skills.Impaired Block Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, impblockprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_impaired_block_probability" },
			{ "Guard Skills.Advance Probability", "Custom Options", "Guard Skills", offsetof(custom_options_type, advprob), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_advance_probability" },
			{ "Guard Skills.Refractory Timer", "Custom Options", "Guard Skills", offsetof(custom_options_type, refractimer), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_refractory_timer" },
			{ "Guard Skills.Extra Strength", "Custom Options", "Guard Skills", offsetof(custom_options_type, extrastrength), T_U16, 12, w, 0, 0, 0, 1, NULL, "property.guard_skills_extra_strength" },
			{ "Level Colours", "Custom Options", "Levels", offsetof(custom_options_type, tbl_level_color), T_U16, 16, w, 0, 0, 0, 1, NULL, "property.level_colours" },
		};
		for (size_t k = 0; k < sizeof e / sizeof e[0]; k++) entry(&b, &e[k]);
	}
	sb_add(&b, "\n  ]\n}\n");
	g_json = b.p;
	return 1;
}
