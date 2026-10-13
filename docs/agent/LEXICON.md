# chaOS personal command lexicon

Deck-facing verbs for `terminal.elf`. The names live in `user/verbs.h`.
The shell matches **only** those strings. Classic names (`ls`, `cd`, `rm`,
`exit`, `sudo`, …) are rejected. Rename a command by editing the `verb`
field; leave `id` alone so the behavior stays put.

`ascend` is the privilege gate. It asks for the logged-in user's password.
System files (`*.elf`, `attune`, `accounts`, `session`) cannot be read or
changed until then.

The first interactive boot (`gate.elf`) asks for a user and a password
and stores them. Every boot lists the account names, then asks for a
username and a password before the desk. The terminal prompt is `name> `,
or `name# ` after `ascend`.

`empower` is the package-upgrade verb. It is recognized and not built yet.

## Desk

Every mode (deck, notes, code) shows a **terminal** icon on the desk.
Click it, or press `t` in command mode. More programs join that icon
list later. `q` and F1 do not leave the desk and do not open the kernel
shell. `banish` closes the terminal and returns to the desk.

`folio.elf` and `scribe.elf` are still programs (`summon folio.elf` finds them in `bin/`).
They are not desk panels. `scribe.elf` is the terminal text editor.
File work goes through the verbs below.

## Verbs

| Verb | Meaning | Now |
|------|---------|-----|
| `lexicon` | List commands | yes |
| `banish` | Exit the terminal | yes |
| `summon PROGRAM` | Run a program and wait. A bare name is looked up in `bin/` | yes |
| `reveal` | List the current directory | yes |
| `wander DIR` | Change directory (`/`, `..`, a relative name) | yes |
| `abode` | Show current directory | yes |
| `forge DIR` | Create a directory | yes |
| `scribe FILE [TEXT]` | `FILE` opens the editor. `FILE TEXT` writes TEXT when the file is new; an existing file opens the editor | yes |
| `glance FILE` | Print a file | yes |
| `duplicate SRC DST` | Copy | yes |
| `relocate SRC DST` | Move or rename | yes |
| `erase FILE` | Delete | yes |
| `seek WORD` | Search names and text | yes |
| `discern FILE` | Identify a file | yes |
| `inspect FILE` | Name, size, flags | yes |
| `observe` | List processes (`id` and name) | yes |
| `oversee` | Same list with state and kernel/user | yes |
| `slay PID` | Kill that user process | admin |
| `obliterate PID` | Kill that process and the user processes it started | admin |
| `freeze PID` | Stop scheduling a user process | admin |
| `awaken PID` | Resume a frozen process | admin |
| `await PID` | Wait for a child | yes (`SYS_WAIT`) |
| `probe` | Ping | no network yet |
| `resolve` | DNS lookup | no network yet |
| `fetch` | HTTP request | no network yet |
| `voyage` | SSH | no network yet |
| `transport` | Secure copy | no network yet |
| `rebirth` | Reboot | admin |
| `slumber` | Shut down (power off) | admin |
| `extinguish` | Power off | admin |
| `age` | Uptime | yes |
| `moment` | Date and time (CMOS clock). `age` stays uptime | yes |
| `essence` | System information | yes |
| `reserves` | Memory information | no user memory stats yet |
| `provisions` | Disk information | yes (one volume, path names) |
| `omens` | Kernel messages | no log yet |
| `acquire` | Install a package | admin, no packages yet |
| `release` | Remove a package | admin, no packages yet |
| `refresh` | Update package lists | admin, no packages yet |
| `empower` | Upgrade packages | admin, no packages yet |
| `possessions` | Installed packages | no packages yet |
| `purge` | Clear the terminal | yes |
| `expel` | Clean cache | admin, no cache yet |
| `conceal FILE` | Hide a file (prefix `.`) | yes |
| `unveil` | Toggle hidden names in `reveal` | yes |
| `manifest USER` | Create a user | admin |
| `exile USER` | Delete a user | admin |
| `key USER` | Set or change that user's password | admin |
| `assume USER` | Become that user | admin |
| `ascend` | Password of the current user; open admin | yes |

Hidden files are names that start with `.`. `reveal` skips them until `unveil`.
