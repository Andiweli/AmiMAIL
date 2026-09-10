# Multi-account patch 2

This follow-up patch corrects runtime and credential-policy issues found while
testing the first multi-account source patch.

## ReAction account tabs

The main-window selector remains below the status area and is still a native
ReAction `clicktab.gadget`. AmigaOS 3.2 exposes orientation and flip constants,
but its clicktab header marks that attribute as not implemented. Consequently,
the tabs cannot be turned upside down by a supported class attribute. The patch
does not replace native rendering with a custom imitation.

After Account settings are saved, AmiMail now follows the dynamic-clicktab
update sequence required by ReAction: detach the old labels, rebuild and attach
the list, then issue `WM_RETHINK` on the live window. This makes the current
set of one to three enabled account tabs visible immediately.

## Independent network subprocess contexts

Every enabled account retains its own `AmgNetwork` worker. On AmigaOS,
`bsdsocket.library` associates opener state with the calling task, so every
worker now opens and closes its own socket-library base. Direct socket calls
resolve that base from the current task.

The AmiSSL instance remains shared to avoid redundant library instances and to
retain AmiSSL's shared certificate cache. In accordance with the AmiSSL v5
autodoc, each subprocess calls `InitAmiSSLA()` with its own socket base and
`errno` pointer and pairs it with `CleanupAmiSSLA()` before that subprocess
releases its socket base. This replaces the first patch's single global user
count, which incorrectly skipped initialization for the second worker.

## Startup and configuration passwords

Application startup no longer opens a master-password requester. AmiMail keeps
a derived 256-bit key per configured account in the account's persistent key
file and uses that key automatically at later starts. The master password itself
is never stored.

The per-account checkbox is now **Allow changes without master password**. Its
meaning is deliberately separate from startup:

- disabled: startup remains automatic, but changing encrypted account settings
  requires the correct master password;
- enabled: future changes may re-encrypt the account through the cached derived
  key, without asking for the master password;
- enabling the option for an existing account still requires the master
  password once;
- an account created by an older version without a persistent key stays locked
  at startup and must be unlocked once in Account settings. That successful
  unlock creates the automatic startup key.

The account metadata stores only this edit policy. Cached saves preserve the
existing PBKDF2 salt and iteration count so the original master password
continues to unlock the rewritten file.

## Verification performed

- portable regression suite: 328 checks, 0 failures;
- whole-source host compilation with the project's warning flags;
- Amiga code-path syntax checks for every changed C file against the AmigaOS
  3.2 NDK and AmiSSL v5 headers;
- German catalog regenerated as an IFF CTLG file.

The archive contains source, not a newly linked m68k executable. A target build
and interactive test with two or three real accounts on AmigaOS 3.2 remain the
final release gate.
