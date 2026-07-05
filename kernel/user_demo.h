#ifndef USER_DEMO_H
#define USER_DEMO_H

/* Spawns a ring-3 user task running the demo program. Returns the task id, or
 * -1 on failure. The task is scheduled like any other and runs concurrently. */
int usermode_spawn();

/* Spawns a ring-3 task that deliberately executes a privileged instruction to
 * verify fault isolation: the kernel must terminate it and keep running. */
int usermode_spawn_faulting();

/* Interactive helper: spawn one user task and block (idling) until it exits. */
void usermode_run();

#endif
