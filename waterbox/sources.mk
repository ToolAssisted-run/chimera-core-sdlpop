# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT := ..
SDL  := $(ROOT)/extern/SDL
POP  := $(ROOT)/extern/SDLPoP/src
MB   ?= $(or $(MINIBOX_DIR),$(HOME)/chimera/extern/chimera-common-minibox)

# ---- SDL 2, upstream, built the way its own Makefile.minimal builds it: the
# platform-neutral config (include/SDL_config_minimal.h - dummy video and
# audio drivers, no threads, no timers, no joysticks, no dynamic loading) and
# the portable C of every subsystem. On top of that:
#   SDL_CPUINFO_DISABLED  SDL asks the CPU nothing, so every blitter is the C
#                         one on every host: the picture cannot depend on the
#                         machine it is drawn on
#   DYNAPI_NEEDS_DLOPEN   (without HAVE_DLOPEN) turns off SDL's dynamic API,
#                         which would otherwise let an environment variable
#                         load another SDL into the process
#   HAVE_MALLOC, HAVE_STDIO_H
#                         SDL allocates and opens files with the C library,
#                         like the game (so its opens go through seams.c), and
#                         not from a dlmalloc of its own beside it
SDL_DIRS := src src/atomic src/audio src/audio/dummy src/cpuinfo src/events src/file \
	src/haptic src/haptic/dummy src/hidapi src/joystick src/joystick/dummy src/loadso/dummy \
	src/power src/filesystem/dummy src/locale src/locale/dummy src/misc src/misc/dummy \
	src/render src/render/software src/sensor src/sensor/dummy src/stdlib src/libm \
	src/thread src/thread/generic src/timer src/timer/dummy src/video src/video/yuv2rgb \
	src/video/dummy
# The minimal configuration passes Sint64 where SDL's own scanners take long
# long - the same 64 bits on this ABI; gcc 14 calls that an error
SDL_WARN := -w -Wno-error=incompatible-pointer-types
SDL_SRCS := $(sort $(foreach d,$(SDL_DIRS),$(wildcard $(SDL)/$(d)/*.c)))
SDL_DEFS := -DSDL_CPUINFO_DISABLED=1 -DDYNAPI_NEEDS_DLOPEN=1 -DHAVE_MALLOC=1 -DHAVE_STDLIB_H=1 -DHAVE_STDIO_H=1 -DHAVE_STRING_H=1

# ---- SDLPoP, upstream, every translation unit but main.c (the core is main)
POP_NAMES := data lighting menu midi opl3 options replay screenshot seg000 seg001 seg002 \
	seg003 seg004 seg005 seg006 seg007 seg008 seg009 seqtbl stb_vorbis
POP_SRCS := $(addprefix $(POP)/,$(addsuffix .c,$(POP_NAMES)))
POP_DEFS := -DCHIMERA_CORE -D_GNU_SOURCE=1 -std=gnu99

# ---- the core
CORE_NAMES := sdlpop-driver game-state seams coroutine sha1 wbx-entry
CORE_HDRS := chimera-hooks.h sdlpop-driver.h coroutine.h sha1.h settings.inc

# ---- the calls the core answers itself (seams.c)
WRAPS := SDL_GetPerformanceCounter SDL_GetPerformanceFrequency SDL_GetTicks SDL_GetTicks64 \
	SDL_Delay SDL_AddTimer SDL_RemoveTimer time SDL_Init SDL_InitSubSystem SDL_OpenAudio \
	SDL_CloseAudio SDL_PauseAudio SDL_LockAudio SDL_UnlockAudio SDL_GetAudioStatus \
	SDL_UpdateTexture SDL_RenderClear SDL_RenderCopy SDL_RenderPresent fopen access stat \
	opendir mkdir exit SDL_PollEvent getenv SDL_GetKeyboardState
WRAP_FLAGS := $(foreach w,$(WRAPS),-Wl,--wrap=$(w))

# the patch series goes onto the submodule before anything of SDLPoP builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# SDLPoP includes <SDL2/SDL.h> and <SDL2/SDL_image.h>: a directory that has
# SDL's headers under SDL2/, and the core's own SDL_image.h (declarations
# only; there is no image loader in the core)
INC_DIR := $(ROOT)/build/include
SDL_HDRS := $(wildcard $(SDL)/include/*.h)
$(INC_DIR)/SDL2/.stamp: $(SDL_HDRS) sdl-image/SDL_image.h
	@mkdir -p $(INC_DIR)/SDL2
	cp $(SDL_HDRS) sdl-image/SDL_image.h $(INC_DIR)/SDL2/
	@touch $@

COMMON_CFLAGS := -O2 -fno-strict-aliasing $(SDL_DEFS) -I$(INC_DIR) -I$(SDL)/include

# Every object depends on the flags it was built with: a change to them
# rebuilds it. (A flag change that rebuilds nothing has bitten these cores: a
# red-zone object two weeks older than the flag that forbade it.)
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
