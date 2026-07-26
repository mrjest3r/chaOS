#ifndef SHELL_H
#define SHELL_H

void shell_prompt();
void shell_execute(char *input);

/* Called from the keyboard IRQ when the user presses Enter. Queues the line for
 * the main loop; never runs commands directly from interrupt context. */
void shell_submit(char *input);

/* Drain one queued command, if any. Call from the idle loop with IF set. */
void shell_poll();

#endif
