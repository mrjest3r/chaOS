#ifndef KEYBOARD_H
#define KEYBOARD_H

void init_keyboard();

/* ---- input focus ----------------------------------------------------------
 * Decides where typed characters go: the kernel shell's line editor, or the
 * ring buffer that user programs read through SYS_GETCHAR / SYS_READLINE. */
#define KBD_FOCUS_SHELL   0
#define KBD_FOCUS_PROGRAM 1

void kbd_set_focus(int focus);

/* ---- program input buffer -------------------------------------------------
 * Raw characters ('\n' for Enter, '\b' for Backspace) queued for user
 * programs. Producer is the keyboard IRQ; consumer is the syscall handler. */
int  kbd_haschar();
char kbd_getchar();               /* non-blocking; returns 0 when empty      */
void kbd_inject(const char *s);   /* fake keystrokes (used by self-tests)    */

#endif
