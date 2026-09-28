# native.mk - the native reference: the same SDL, SDLPoP and core sources as
# guest.mk, built for the host, plus the two harnesses (run-native drives the
# exports directly; run-wbx drives core.wbx through the miniBox host exactly as
# the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

SDL_CFLAGS := $(COMMON_CFLAGS) $(SDL_WARN)
POP_CFLAGS := $(COMMON_CFLAGS) $(POP_DEFS) -I. -I$(POP) -w
CORE_CFLAGS := $(COMMON_CFLAGS) $(POP_DEFS) $(MBINCS) -I. -I$(POP) -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(SDL_CFLAGS) | $(POP_CFLAGS) | $(CORE_CFLAGS))

SDL_OBJS := $(patsubst $(SDL)/%.c,$(B)/sdl/%.o,$(SDL_SRCS))
POP_OBJS := $(patsubst $(POP)/%.c,$(B)/pop/%.o,$(POP_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_NAMES)))

all: $(B)/run-native $(B)/run-wbx

$(B)/sdl/%.o: $(SDL)/%.c $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(SDL_CFLAGS) -c -o $@ $<

$(B)/pop/%.o: $(POP)/%.c $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags chimera-hooks.h
	@mkdir -p $(dir $@)
	gcc $(POP_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c gate-harness.h sdlpop-driver.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(POP_OBJS) $(SDL_OBJS) $(B)/core/run-native.o
	gcc -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h sdlpop-driver.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean
