/* settings.inc - the settings that are SDLPoP's own options.
 *
 * Each becomes one line of the SDLPoP.ini the core hands the game (the game
 * reads its options with its own parser, so a setting means exactly what
 * SDLPoP says it means). gen-config.py writes waterbox.config's settings from
 * this file and tests/check-wire.py checks the two agree, so there is one list.
 *
 * POP_SETTING(name, kind, ini section, default, min, max, "display", "description")
 * kind BOOL: default 0/1, min/max unused; kind INT: a number within min..max.
 * The name is SDLPoP's own ini option name.
 *
 * Defaults are the original 1.0 game: copy protection on (the potion level after
 * level 1), sixty minutes, three hit points, no fixes. The individual fixes
 * default to SDLPoP's own defaults (on) and do nothing until
 * use_fixes_and_enhancements is on - as in SDLPoP.ini.
 *
 * Two settings are not ini options and live in the driver: sound (the sound
 * card) and random_seed.
 */
POP_SETTING(enable_copyprot, BOOL, "General", 1, 0, 0, "Copy protection",
	"The original game's copy protection: after level 1 comes the potions level, where the prince must drink the potion whose letter the manual gives. On in 1.0; SDLPoP turns it off by default.")
POP_SETTING(first_level, INT, "CustomGameplay", 1, 1, 14, "First level",
	"The level a new game starts on (1 in the original game).")
POP_SETTING(skip_title, BOOL, "CustomGameplay", 0, 0, 0, "Skip the title",
	"Start the first level at once instead of playing the title sequence.")
POP_SETTING(start_minutes_left, INT, "CustomGameplay", 60, -1, 32767, "Minutes",
	"Minutes on the clock at the start (60 in the original game). -1 is no time limit.")
POP_SETTING(start_hitp, INT, "CustomGameplay", 3, 1, 10, "Hit points",
	"Hit points at the start of the game (3 in the original game).")
POP_SETTING(max_hitp_allowed, INT, "CustomGameplay", 10, 1, 10, "Most hit points",
	"The most hit points the prince can reach with life potions (10 in the original game).")
POP_SETTING(use_fixes_and_enhancements, BOOL, "Enhancements", 0, 0, 0, "Fixes and enhancements",
	"SDLPoP's fixes for the original game's bugs and its gameplay enhancements. Off is the original game; on applies the ones below that are on.")
POP_SETTING(enable_crouch_after_climbing, BOOL, "Enhancements", 1, 0, 0, "Crouch after climbing",
	"Pressing down and forward together right after climbing up crouches (the original always climbs back down).")
POP_SETTING(enable_freeze_time_during_end_music, BOOL, "Enhancements", 1, 0, 0, "Freeze time during the end music",
	"The clock stops while a level's ending music plays (in the original it keeps running).")
POP_SETTING(enable_remember_guard_hp, BOOL, "Enhancements", 1, 0, 0, "Remember guard hit points",
	"A guard keeps the hit points he had when the prince comes back into his room.")
POP_SETTING(enable_super_high_jump, BOOL, "Enhancements", 1, 0, 0, "Super high jump",
	"In feather fall (after the green potion) the prince can jump two stories up.")
POP_SETTING(enable_jump_grab, BOOL, "Enhancements", 1, 0, 0, "Jump grab",
	"The prince can grab the floor above while jumping (Shift and Up without forward).")
POP_SETTING(fix_gate_sounds, BOOL, "Enhancements", 1, 0, 0, "Fix: gate sounds",
	"If a room is linked to itself on the left, the closing sounds of its gates can be heard.")
POP_SETTING(fix_two_coll_bug, BOOL, "Enhancements", 1, 0, 0, "Fix: two collision bug",
	"An open gate or chomper no longer lets the prince go through walls (tricks 7, 37, 62).")
POP_SETTING(fix_infinite_down_bug, BOOL, "Enhancements", 1, 0, 0, "Fix: infinite fall",
	"A room linked to itself at the bottom, with no floor in the prince's column, no longer hangs the game.")
POP_SETTING(fix_gate_drawing_bug, BOOL, "Enhancements", 1, 0, 0, "Fix: gate under a gate",
	"The top of a gate under another gate is drawn.")
POP_SETTING(fix_bigpillar_climb, BOOL, "Enhancements", 1, 0, 0, "Fix: big pillar climb",
	"Climbing up to a floor with a big pillar top behind, turned right, no longer sees through the floor.")
POP_SETTING(fix_jump_distance_at_edge, BOOL, "Enhancements", 1, 0, 0, "Fix: jump distance at an edge",
	"Climbing up two floors, turning and jumping up no longer makes the prince fall (trick 25 no longer needed).")
POP_SETTING(fix_edge_distance_check_when_climbing, BOOL, "Enhancements", 1, 0, 0, "Fix: edge check when climbing",
	"Climbing to a higher floor no longer checks the edge below, which could teleport the prince.")
POP_SETTING(fix_painless_fall_on_guard, BOOL, "Enhancements", 1, 0, 0, "Fix: painless fall on a guard",
	"Falling from a great height onto a guard hurts.")
POP_SETTING(fix_wall_bump_triggers_tile_below, BOOL, "Enhancements", 1, 0, 0, "Fix: wall bump drops the tile below",
	"Bumping against a wall no longer drops a loose floor below (tricks 18, 34).")
POP_SETTING(fix_stand_on_thin_air, BOOL, "Enhancements", 1, 0, 0, "Fix: standing on thin air",
	"Standing up from a crouch on a falling loose tile no longer stands on thin air.")
POP_SETTING(fix_press_through_closed_gates, BOOL, "Enhancements", 1, 0, 0, "Fix: buttons through closed gates",
	"Buttons right of a closed gate can no longer be pressed through it (trick 1).")
POP_SETTING(fix_grab_falling_speed, BOOL, "Enhancements", 1, 0, 0, "Fix: grab two stories down",
	"Jumping and bumping into a wall no longer grabs a ledge two stories down.")
POP_SETTING(fix_skeleton_chomper_blood, BOOL, "Enhancements", 1, 0, 0, "Fix: skeleton blood",
	"A chomper that chomps a skeleton does not become bloody.")
POP_SETTING(fix_move_after_drink, BOOL, "Enhancements", 1, 0, 0, "Fix: move after drinking",
	"The controls are released properly after drinking a potion.")
POP_SETTING(fix_loose_left_of_potion, BOOL, "Enhancements", 1, 0, 0, "Fix: loose floor left of a potion",
	"Drawing of a loose floor to the left of a potion or sword.")
POP_SETTING(fix_guard_following_through_closed_gates, BOOL, "Enhancements", 1, 0, 0, "Fix: guards through closed gates",
	"Guards no longer follow the prince to the next room through a closed gate.")
POP_SETTING(fix_safe_landing_on_spikes, BOOL, "Enhancements", 1, 0, 0, "Fix: safe landing on spikes",
	"Landing on the edge of a spikes tile is no longer safe (trick 65).")
POP_SETTING(fix_glide_through_wall, BOOL, "Enhancements", 1, 0, 0, "Fix: glide through walls",
	"Turning around while running no longer glides through walls.")
POP_SETTING(fix_drop_through_tapestry, BOOL, "Enhancements", 1, 0, 0, "Fix: drop through a tapestry",
	"The prince can no longer drop through a closed gate with a tapestry above it.")
POP_SETTING(fix_land_against_gate_or_tapestry, BOOL, "Enhancements", 1, 0, 0, "Fix: land against a gate",
	"Landing against a closed gate or a tapestry plays the whole landing animation.")
POP_SETTING(fix_unintended_sword_strike, BOOL, "Enhancements", 1, 0, 0, "Fix: unintended sword strike",
	"Drawing the sword no longer sometimes strikes at once.")
POP_SETTING(fix_retreat_without_leaving_room, BOOL, "Enhancements", 1, 0, 0, "Fix: retreat out of a room",
	"Retreating in a sword fight can no longer leave a room without the room changing (trick 35).")
POP_SETTING(fix_running_jump_through_tapestry, BOOL, "Enhancements", 1, 0, 0, "Fix: running jump through a tapestry",
	"A running jump to the left no longer goes through a tapestry with a floor above.")
POP_SETTING(fix_push_guard_into_wall, BOOL, "Enhancements", 1, 0, 0, "Fix: guards pushed into walls",
	"Guards can no longer be pushed into walls.")
POP_SETTING(fix_jump_through_wall_above_gate, BOOL, "Enhancements", 1, 0, 0, "Fix: jump through a wall above a gate",
	"A running jump into a wall no longer falls behind a closed gate two floors down (the level 7 skip).")
POP_SETTING(fix_chompers_not_starting, BOOL, "Enhancements", 1, 0, 0, "Fix: chompers not starting",
	"Grabbing a ledge a floor or more down starts the chompers on that row.")
POP_SETTING(fix_feather_interrupted_by_leveldoor, BOOL, "Enhancements", 1, 0, 0, "Fix: feather fall and the level door",
	"A level door finishing opening no longer ends feather fall.")
POP_SETTING(fix_offscreen_guards_disappearing, BOOL, "Enhancements", 1, 0, 0, "Fix: offscreen guards disappearing",
	"Guards pushed offscreen reappear in the next room.")
POP_SETTING(fix_move_after_sheathe, BOOL, "Enhancements", 1, 0, 0, "Fix: move after sheathing",
	"Releasing down while putting the sword away no longer ducks.")
POP_SETTING(fix_hidden_floors_during_flashing, BOOL, "Enhancements", 1, 0, 0, "Fix: hidden floors while flashing",
	"After uniting with the shadow on level 12 the hidden floors appear at once.")
POP_SETTING(fix_hang_on_teleport, BOOL, "Enhancements", 1, 0, 0, "Fix: hang-on teleport",
	"Grabbing a ledge at a bottom corner of a room no longer teleports to the room above.")
POP_SETTING(fix_exit_door, BOOL, "Enhancements", 1, 0, 0, "Fix: closed exit doors",
	"Closed exit doors can no longer be entered after meeting the shadow, after Jaffar dies, or after opening another exit.")
POP_SETTING(fix_quicksave_during_feather, BOOL, "Enhancements", 1, 0, 0, "Fix: feather fall timing",
	"Feather fall keeps its length when the game switches between the walking and fighting speeds.")
POP_SETTING(fix_caped_prince_sliding_through_gate, BOOL, "Enhancements", 1, 0, 0, "Fix: sliding through a gate",
	"Crouching with the back to a closed gate at a room's left edge no longer slides through it.")
POP_SETTING(fix_doortop_disabling_guard, BOOL, "Enhancements", 1, 0, 0, "Fix: guards on a door top",
	"A guard or prince standing on a door top no longer makes guards inactive.")
POP_SETTING(fix_jumping_over_guard, BOOL, "Enhancements", 1, 0, 0, "Fix: jumping over a guard",
	"A timed running jump no longer jumps over a guard.")
POP_SETTING(fix_falling_through_floor_during_sword_strike, BOOL, "Enhancements", 1, 0, 0, "Fix: falling during a strike",
	"The prince or a guard no longer falls through the floor during a sword strike.")
POP_SETTING(fix_register_quick_input, BOOL, "Enhancements", 1, 0, 0, "Fix: quick key presses",
	"A key pressed and released between two ticks still counts.")
POP_SETTING(fix_turn_running_near_wall, BOOL, "Enhancements", 1, 0, 0, "Fix: turning to run near a wall",
	"Turning to run right next to a wall, gate or tapestry safe-steps as facing it would (tricks 1 and 2).")
POP_SETTING(fix_feather_fall_affects_guards, BOOL, "Enhancements", 1, 0, 0, "Fix: feather fall and guards",
	"Feather fall affects only the prince, not the guards.")
POP_SETTING(fix_one_hp_stops_blinking, BOOL, "Enhancements", 1, 0, 0, "Fix: last hit point blinking",
	"A single hit point keeps blinking after Jaffar is defeated.")
POP_SETTING(fix_dead_floating_in_air, BOOL, "Enhancements", 1, 0, 0, "Fix: bodies in the air",
	"A body on a loose floor that drops falls with it.")
