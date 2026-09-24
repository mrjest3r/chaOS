#ifndef SHELL_H
#define SHELL_H

void shell_prompt();
void shell_execute(char *input);

/* Called from the keyboard IRQ when the user presses Enter. Queues the line for
 * the main loop; never runs commands directly from interrupt context. */
void shell_submit(char *input);

/* Drain one queued command, if any. Call from the idle loop with IF set. */
void shell_poll();

/* Line editor (kernel shell). Keyboard / mouse feed these while focus=shell. */
void shell_input_char(char c);
void shell_input_backspace(void);
void shell_input_enter(void);
void shell_input_left(void);
void shell_input_right(void);
void shell_input_home(void);
void shell_input_end(void);
void shell_input_click(int px, int py);

#endif
