# Multi-account patch 1

This source patch extends AmiMail 1.5 from one account to up to three
independently configured accounts.

## User interface

- Account settings use the native ReAction `clicktab.gadget` with the fixed
  pages Account 1, Account 2, and Account 3.
- Every page exposes the complete existing account form, including its own
  fetch schedule, notification enable switch, and notification sound path.
- An account is included in background work and in the main selector only
  when its `Active account` checkbox is selected. At least one account must
  remain active.
- The main window uses a native dynamic `clicktab.gadget` below the status
  area. It contains only active accounts and displays their configured email
  addresses. No custom tab renderer or non-ReAction orientation is used.

AmigaOS 3.2 defines horizontal/vertical/flip orientation constants for
`clicktab.gadget`, but the class header marks the orientation attribute as not
implemented. The bottom selector therefore remains a standard, unflipped
native clicktab row.

## Runtime and persistence

Each account has its own account model, encrypted configuration, session key,
persistent unlock key, network worker, inbox UID baseline, unseen counter, and
notification behavior. Active accounts may fetch in parallel; only the
selected account feeds folder/message data into the visible main window.

Slot 1 retains all original paths, so existing installations migrate without
renaming files. Slots 2 and 3 use suffixed files:

| Data | Slot 1 | Slot 2 | Slot 3 |
| --- | --- | --- | --- |
| Account | `account.cfg` | `account-2.cfg` | `account-3.cfg` |
| Session key | `AmiMail.session-key` | `AmiMail.session-key-2` | `AmiMail.session-key-3` |
| Persistent key | `account.key` | `account-2.key` | `account-3.key` |
| Inbox state | `inbox-notify.state` | `inbox-notify-2.state` | `inbox-notify-3.state` |

An older account file without an `enabled` field is treated as active. Slots
without an account file default to inactive, except the first slot on a fresh
installation so that the existing first-run flow remains intact.

## Verification performed

- Host regression suite: 327 checks, 0 failures.
- Whole-source host compilation check with the project's warning flags.
- Amiga code-path syntax check for all changed C files against the AmigaOS 3.2
  NDK and AmiSSL headers.
- German catalog rebuilt and validated as an IFF CTLG message catalog.

The archive is source-only. A final m68k link and interactive test on a real or
emulated AmigaOS 3.2 system remain necessary before producing a release
binary.
