# chaOS personal command lexicon

Deck-facing verbs used by `ush.elf`. Technical aliases (`ls`, `run`, `exit`, …)
still work so scripts and AUTOTEST stay stable.

## Deck modes (P4.2)

| Verb | Meaning |
|------|---------|
| `attune MODE` | Switch session mode: `deck` \| `notes` \| `code` |
| `mode MODE` | Alias for `attune` |

| Mode | Prompt | Command set |
|------|--------|-------------|
| `deck` | `ush>` | All verbs |
| `notes` | `ush[notes]>` | folio, note, scrap, scribe, peek, stash, roster |
| `code` | `ush[code]>` | summon/ignite/loose, scribe, peek, stash, roster |

Always available: `lexicon`, `attune`, `pulse`, `who`, `slumber`, `banish`.

## Verbs

| Verb | Meaning | Alias / notes |
|------|---------|----------------|
| `lexicon` / `help` | List verbs for current mode | |
| `roster` | List files on disk | `ls` |
| `folio` | Files + notes browser | `folio.elf` (notes/deck) |
| `note FILE` | Create/open note | scribe (notes/deck) |
| `scrap FILE` | Delete a file | notes/deck |
| `summon FILE` | Run ELF and wait | `run` — code/deck |
| `ignite FILE` | Run ELF and wait | code/deck |
| `loose FILE` | Run ELF in background | `bg` — code/deck |
| `scribe FILE` | Edit text file | |
| `peek FILE` | Print file contents | |
| `stash FILE TEXT` | Write text into file | |
| `pulse` | Timer ticks since boot | `uptime` |
| `who` | Current task id | `pid` |
| `slumber` | Blank display until key/mouse | `doze`; kernel shell: `doze` |
| `banish` | Leave ush → kernel shell | `exit` |

In **folio**: `roster`/`r`/`ls`, `peek`/`type`, `open`, `note`, `scrap`,
`banish`/`q`, `lexicon`/`h`.

In **chaoswm**: `s`/`b` summon/banish windows, `+/-` chaos, `q`/Esc quit.

**scribe** (`h` for help): `p` print, `a` append (end with `.`), `e N TEXT`,
`d N`, `w`/`wq` save, `q`/`q!` quit.

Host-built ELFs: see `tools/sdk/README.md`.
