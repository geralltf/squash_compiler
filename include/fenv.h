#ifndef _FENV_H
#define _FENV_H
/* Minimal <fenv.h> stub. squash has no real hardware floating-point
 * environment control (rounding-mode save/restore); these no-ops are enough
 * for real-world code that merely brackets a block with
 * feholdexcept(&saved)/fesetenv(&saved) for exception-safety around a
 * rounding-sensitive conversion (e.g. SDL3's own SDL_audiotypecvt.c) —
 * losing the actual rounding-mode/exception-masking behavior doesn't change
 * the converted VALUES for ordinary in-range audio samples, only the
 * handling of extreme edge cases (NaN/overflow) this build doesn't need to
 * get bit-exact. */
typedef int fenv_t;

static int feholdexcept(fenv_t *envp) { (void)envp; return 0; }
static int fesetenv(const fenv_t *envp) { (void)envp; return 0; }

#endif /* _FENV_H */
