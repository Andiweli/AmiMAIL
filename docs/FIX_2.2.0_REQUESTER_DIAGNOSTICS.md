# AmiMAIL 2.2.0 — Confirmation diagnostics and failure safety

## Scope and source baseline

This patch addresses AmiMAIL's previously silent confirmation failure paths.
It does **not** establish or fix the underlying reported interaction with Gloom
Reforged, nor does it change that program. A failed/suppressed requester after
another program exits still needs a reproduction log from the affected Amiga.
The unrelated AmigaOS 3.3 startup crash is outside this patch.

The baseline was reconstructed from the supplied, successive source archives:
the accepted 2.2.0 native-checkbox/reply-delete/sound-Herald fixes, the latest
progress cancel-width fix, and the rebuilt contact headers with icons lowered
by one pixel. `gui_dialogs.c` comes from the native-checkbox patch;
`gui_actions.c` comes from the reply/delete patch. No repository revision or
rejected preview/contact-header experiment replaces those current files.

Program version remains **2.2.0** and the output remains **bin/AmiMAIL**.
Catalog generation is now **13** (571 existing and new entries combined).
Only two new messages were added; existing IDs/translations retain their values.

## Changes

### Explicit failure reporting

The shared confirmation function still returns **1 only for an affirmative
answer, and 0 otherwise**. Negative error codes are deliberately not returned:
legacy callers test this as a Boolean, so a negative number would be unsafe.

`AmgGui.confirmation_failure` independently records construction failure.
It is reset on every new requester attempt. A failed construction now shows a
localized status-line message with the relevant R-code, rather than pretending
that the user clicked No. No destructive network command is sent in this case.

| Code | Failure stage |
| --- | --- |
| R1 | Missing GUI, reference window, or screen |
| R2 | ReAction layout construction returned NULL |
| R3 | WindowObject construction returned NULL |
| R4 | RA_OpenWindow could not open the actual window |
| R5 | The opened window has no usable WINDOW_SigMask |

These codes describe **where** the operation failed, not why. In particular,
a constructor returning NULL does not by itself prove a lack of free RAM.
The immediately sampled `IoErr()` is logged as context, not a guaranteed
BOOPSI failure reason. Disposal is performed before showing the failure status.

The requester wait loop now latches its first decision. No, Escape and Close
cannot be overwritten by a subsequent queued Yes/Return. Ctrl-C takes priority
over queued affirmative events. No pending signal is cleared by the logger.

If the draft-save confirmation cannot be created/opened, the composer and its
unsaved text/attachments remain open. An explicit, normally processed No
retains its existing behaviour. This is not a redesign of all composer exit
paths or of the user's Ctrl-C policy.

### Unchanged UI and behaviour

The shared requester's layout and WindowObject tag blocks are byte-for-byte
unchanged. Native ReAction still owns the same gadgets and rendering. The
pre-open geometry calculation, Yes/No arrangement, fonts and spacings remain.
There is no alternate requester backend, no automatic affirmative fallback,
and no class/library reloading, memory flushing or input-system patch.

The source files for contact sorting, arrow placement, mail preview, status
progress, X-button width, IMAP protocol, account setup controls and sound/
Herald playback are not replaced or changed by this package.

## Installation

Apply all files in this package over the accepted 2.2.0 source tree. Then:

```text
make clean
make
```

A clean build is recommended because the private GUI structure gained a field.
The Makefile includes the new `src/gui_requester_diag.c` translation unit.
Python remains a build-host requirement only; the Amiga does not need it.

Close AmiMAIL. On the Amiga replace:

- the executable with the new `bin/AmiMAIL`;
- `Catalogs/deutsch/AmiMAIL.catalog` with the included German V13 catalog.

The runtime catalog version comes from `include/catalog_ids.h`; `i18n.c`
therefore needs no edit. Its existing older-catalog fallback is preserved.
Leaving an older catalog installed can make just the new messages English.

## Reproduction and logging

For a full deletion trace, run this in an Amiga Shell **before reproducing**:

```text
SetEnv AmiMAIL_RequesterDebug 1
```

This is a volatile global environment setting, also visible to an icon-started
AmiMAIL. No permanent configuration setting or new account preference is used.

Reproduce the same sequence: start and exit Gloom, start AmiMAIL, select a mail,
and press Delete. Copy **T:AmiMAIL-requesters.log** to a persistent location
before rebooting. A normal successful run and a failing run are both useful.
The log is appended across AmiMAIL restarts until it reaches 256 KiB. It is
never silently truncated. If full, save it and remove it before another test.

After the test, disable full tracing with:

```text
SetEnv AmiMAIL_RequesterDebug 0
```

Construction failures (R1–R5) still make one best-effort log entry without the
switch. Normal clicks, decisions and precondition failures are logged only
when tracing is enabled. Diagnostic I/O failure is silent and cannot authorize
or prevent a delete; the explicit requester failure status still appears.

The logger uses fixed-size formatting and unbuffered DOS file operations,
closes its file each time, and preserves both `IoErr()` and C `errno`.
It temporarily suppresses DOS file-error requesters for its own I/O only, then
restores the exact prior `pr_WindowPtr`, including an inherited -1.
This is not an attempt to globally reset or repair requester settings.

No body, subject, email address, account name, mailbox name, password or key is
written. The log does include account slot, message UID, pointer values,
signal masks, sizes and module versions. Only GUI-process code calls it;
it is never called from a render hook, an input.device hook or an interrupt.

## Reading the trace

The prefix identifies program version, task address, per-process sequence and
an Amiga DateStamp (days/minutes/ticks). The fields `v1` and `v2` depend on stage:

| Stage | Values / interpretation |
| --- | --- |
| `delete.dispatch` | Delete command reached the main gadget handler |
| `delete.begin` | Actual delete function entered |
| `delete.selection` | Number of selected UIDs; connected flag |
| `delete.blocked.*` | Allocation, no selection, offline, no folder, in Trash |
| `requester.enter` | Reference window address; requested minimum width |
| `requester.layout` | Layout constructor succeeded |
| `requester.minimum` | Measured minimum inner width/height |
| `requester.position` | Planned left/top coordinates |
| `requester.size` | Planned outer width/height |
| `requester.object` | WindowObject constructor succeeded |
| `requester.open` | Actual requester window pointer; window signal mask |
| `requester.failure` | R-code; immediately captured IoErr context |
| `requester.signals` | Wait result; window signal mask |
| `requester.input` | Actual RA_HandleInput result |
| `requester.cancel.*` | Close, Escape, No or Ctrl-C ended the question |
| `requester.accept.*` | Yes or Return/Enter confirmed the question |
| `requester.result` | Final Boolean answer |
| `delete.not-confirmed` | No delete queued; v1 is the construction failure code |
| `delete.confirmed` | Number of messages explicitly confirmed |
| `delete.enqueue` | UID; local queue result (not server acknowledgement) |
| `empty_trash.begin` / `empty_spam.begin` | Action entered; connected flag |

A trace stopping before `requester.enter` points to a preceding gate rather
than a failed window construction. `requester.open` followed immediately by
a cancel event means the requester did open and was subsequently closed.
`delete.enqueue` success only proves queue insertion; it does not by itself
prove that the server completed deletion.

Each written record includes free Chip/Fast memory, largest free blocks,
currently pending task signals, the original requester-window pointer, and
window/layout/button library versions. These are diagnostic snapshots, not
allocation guarantees. Exec's MEMF_LARGEST query also checks free-list
consistency; pre-existing heap corruption can be detected by that check.
Logging changes timing and memory/I/O conditions and can affect a sporadic
failure. No log entry alone diagnoses damage caused by another program.

## Verification performed

- Reconstructed baseline: `make -j3 review-test` passed before editing.
- Patched source: complete `make -j3 review-test` passed with GCC.
- `make host-check` passed (non-Amiga branches only).
- New tests execute the actual requester, logger, delete function and draft
  cancel branch against explicit native-API doubles (3,224 assertions).
- GCC and Clang `-Werror` runs passed for the new tests.
- GCC and Clang AddressSanitizer/UndefinedBehaviorSanitizer runs passed for the
  new tests; the complete existing suite was not rerun under every sanitizer.
- GCC/Clang native-branch syntax checks include the full new diagnostic module,
  using explicit API declarations, not an Amiga SDK.
- Tests inject failure at every R-stage, no-selection/offline/Trash gates,
  queue failure, immediate Ctrl-C, competing Yes/No events, and diagnostic
  open/seek/write/full-log failures. They check signal/error/requester-pointer
  preservation and prevent draft discard on a failed confirmation.
- Catalog build/readback and translations/format arguments passed.
- SHA-256 tests pin the unchanged native requester construction/tag blocks.

There is **no actual m68k build**, real ReAction rendering test, or reproduction
of the Gloom/AmiMAIL failure here. These tests cannot prove that the original
interaction has been fixed. The purpose is to close confirmed safety/diagnostic
gaps and get evidence from the affected machine without redesigning its UI.

## API references

The implementation follows the documented classic Amiga contracts:

- Exec AvailMem (snapshot, MEMF_LARGEST and free-list consistency):
  https://developer.amigaos3.net/autodocs/exec.library/AvailMem.html
- Exec SetSignal (SetSignal(0, 0) observes without changing signals):
  https://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_3._guide/node023A.html
- Classic DOS GetVar/SetIoErr and related file functions:
  https://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_2._guide/node059A.html
- Classic window.class WINDOW_SigMask definition:
  https://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_3._guide/node0652.html
