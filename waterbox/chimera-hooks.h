/* chimera-hooks.h - the three seams patch 0001 puts into SDLPoP.
 *
 * SDLPoP's common.h includes this when CHIMERA_CORE is defined; nothing else
 * of upstream sees it. Each call is one line at a place where the DOS game
 * waited or looked at the controls, and each is a no-op in upstream's own
 * build. What they mean is the core's business (sdlpop-driver.c):
 *
 *   chimera_wait_for_timer(t)  do_simple_wait() is about to wait for game timer
 *                              t without looking at the controls: the step ends
 *                              there and the clock resumes at t's deadline, so a
 *                              tick of play is one step, not five 1/60 s ones.
 *   chimera_input_read()       the game looked at the controls this step (the
 *                              keyboard or joystick state, or the last key).
 *   chimera_idle_step()        a loop that waits for a key without letting any
 *                              time pass (a message box, the Hall of Fame name
 *                              entry): each pass is one step of 1/60 s.
 */
#ifndef CHIMERA_HOOKS_H
#define CHIMERA_HOOKS_H

void chimera_wait_for_timer(int timer_index);
void chimera_input_read(void);
void chimera_idle_step(void);

#endif
