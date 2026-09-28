# guest.mk - core.wbx: the same SDL, SDLPoP and core sources as native.mk,
# built with miniBox's musl guest toolchain (C only) and linked at the guest
# base. Objects land in build/guest; core.wbx is checked by miniBox's
# check-wbx.sh (no thread-local storage, no %fs, no red zone) before it counts
# as built.
#
# Usage: make -f guest.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-linux
CC     := $(MBUILD)/musl-gcc

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a C guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

SDL_CFLAGS := $(WBFLAGS) $(COMMON_CFLAGS) $(SDL_WARN)
POP_CFLAGS := $(WBFLAGS) $(COMMON_CFLAGS) $(POP_DEFS) -I. -I$(POP) -w
CORE_CFLAGS := $(WBFLAGS) $(COMMON_CFLAGS) $(POP_DEFS) $(MBINCS) -I. -I$(POP) -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(SDL_CFLAGS) | $(POP_CFLAGS) | $(CORE_CFLAGS))

SDL_OBJS := $(patsubst $(SDL)/%.c,$(B)/sdl/%.o,$(SDL_SRCS))
POP_OBJS := $(patsubst $(POP)/%.c,$(B)/pop/%.o,$(POP_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_NAMES)))

all: $(B)/core.wbx

$(CC):
	@echo "miniBox's C guest toolchain is missing: $(CC)" >&2
	@echo "build it: meson setup $(MB)/build/meson-linux $(MB) && ninja -C $(MB)/build/meson-linux" >&2
	@false

$(B)/sdl/%.o: $(SDL)/%.c $(INC_DIR)/SDL2/.stamp $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(SDL_CFLAGS) -c -o $@ $<

$(B)/pop/%.o: $(POP)/%.c $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags chimera-hooks.h | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(POP_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core.wbx: $(CORE_OBJS) $(POP_OBJS) $(SDL_OBJS)
	$(CC) -static -no-pie -Wl,--eh-frame-hdr,-O2 -Wl,-z,stack-size=8388608 -T $(MB)/source/guest/linkscript.T \
		-o $@.tmp $^ $(MBUILD)/source/guest/emulibc.c.o $(WRAP_FLAGS) -lgcc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
