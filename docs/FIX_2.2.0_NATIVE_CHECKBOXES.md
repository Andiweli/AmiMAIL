# AmiMAIL 2.2.0 - Native checkbox correction

## Scope and installation

Apply this changed-files archive over the last AmiMAIL 2.2.0 reply/IMAP
correction and its preceding Herald/subject/retrieval-interval updates.
The reviewed `gui_dialogs.c` is retained as the baseline; no earlier preview
experiment or rollback is used.

Only two application source files change:

- `src/gui.c`: open, require and release `gadgets/checkbox.gadget`.
- `src/gui_dialogs.c`: create nine genuine checkbox objects instead of
  toggle buttons that permanently display the `BAG_CHECKBOX` glyph.

The remaining files contain regression tests, a host-test Makefile target,
the changelog and this report. All changed files are supplied in full.

Run `make` after copying the files into the source tree. On the Amiga replace
the executable with the newly built `bin/AmiMAIL` and restart AmiMAIL.
Program version 2.2.0, German catalog V12 and both guides are unchanged.
No catalog rebuild/reinstallation is needed specifically for this correction.
No native binary or system class is bundled.

## Implementation

`CheckBoxBase` uses the name declared by `<proto/checkbox.h>`. It is opened
with `OpenLibrary("gadgets/checkbox.gadget", 44)` alongside the other required
ReAction classes. Failure follows the existing required-class error path;
it does not silently fall back to the incorrect button representation.
All GUI objects are disposed before closing their class libraries.

A small private constructor uses `CHECKBOX_GetClass()` and `NewObjectA()`
with an explicit, terminated `struct TagItem` array. Its attributes are
`GA_ID`, `GA_RelVerify` and `CHECKBOX_Checked`. No button-only attributes,
manual checkmark images, public class-name lookup, custom checkbox subclass
or extra manual toggle are involved.

In the classic SDK, `CHECKBOX_Checked` is an alias for `GA_Selected`.
Consequently the existing `GetAttr(GA_Selected, ...)` and
`SetGadgetAttrs(..., GA_Selected, ...)` paths remain unchanged. Gadget IDs,
configuration field names, defaults and save/cancel logic are not changed.
The operating-system checkbox class owns selection rendering.

Converted controls:

1. Activate account.
2. IMAP STARTTLS.
3. SMTP STARTTLS.
4. SMTP uses the same credentials.
5. Fetch mail at startup.
6. Periodic fetch.
7. Notification sound.
8. Herald notifications.
9. Save sent mail via IMAP, in the system-folder dialog.

Existing labels remain separate layout children; their text, column widths,
24-pixel checkbox slots, row spacing and the pre-open centering calculation
are preserved. The native checkbox class determines its glyph and minimum
height; no pixel-perfect rendering claim is made from host tests.

The periodic interval chooser continues to become available only when
periodic fetch is selected. The SMTP username/password controls continue to
be disabled when the shared-credentials option is selected. Those event
handlers only read the selection state already supplied by the gadget.
Selecting a sound still sets the existing notification checkbox via
`GA_Selected`. Herald's question-mark test button is unchanged.

## Verification actually performed

### Source and lifecycle checks

- Confirmed all nine old `BAG_CHECKBOX`/`BUTTON_PushButton` constructors were
  removed from `gui_dialogs.c`, and their IDs and initial fields were retained.
- Compared the application-source diff: no state-collection, state-display,
  event-handling, account-ordering, save/cancel, layout-child or locale-text
  logic was modified outside the explicit constructor replacements.
- Tested the actual extracted `open_classes()` / `close_classes()` functions
  with explicit library doubles: normal startup, each of 15 individual
  open failures, required-versus-optional behavior, complete cleanup and
  harmless repeated cleanup. Checkbox absence correctly fails initialization.
- Tested constructor failure for missing base, unavailable class and failed
  object creation, plus boolean normalization of nonzero initial values.

### State and persistence regression tests

The actual checkbox constructor, nine production call sites,
`account_page_show()`, `account_page_collect()`, and dependent-field event
handlers are compiled into the host preferences harness. ReAction objects
are explicit test doubles; the account serializer/deserializer is the real
portable production code.

- All 512 combinations of the nine flags: initial construction, native class
  identity, gadget IDs, release verification and `GA_Selected` values.
- Round-trip collection, save to a temporary configuration file and reload
  for all 512 combinations, rotating through the five account slots.
- Periodic interval preservation and enabled/disabled chooser state.
- SMTP credential-field activation without a second application-side toggle.
- Discarding edited gadget states leaves the source account unchanged;
  showing that account again restores the original states.
- Existing retrieval-row construction, label alignment, small button widths,
  six intervals and account-switch tests remain in place.

These tests run through `make herald-retrieval-test` and
`make checkbox-lifecycle-test`, both included in `make review-test`.

### Compiler and regression results

- Baseline `make -j2 review-test`: PASS before modifying the sources.
- Patched `make -j2 review-test` with GCC and `-Werror`: PASS.
- Extended checkbox/preferences and library-lifecycle tests with Clang and
  `-Werror`: PASS.
- The extended tests with AddressSanitizer, UndefinedBehaviorSanitizer and
  leak detection under GCC and Clang: PASS.
- Existing IMAP, MIME, file/attachment, catalog, sound, Herald, account-order,
  progress-context, per-account timer and Reply-popup regression suites: PASS.
- Catalog check: unchanged V12, 569 IDs, translations and binary verified.

The host suite's native-API portions use test doubles. No full
`m68k-amigaos-gcc` build, m68k ABI/link test, real ReAction mouse activation,
checkbox pixel rendering or real mail-server session was performed here.
The tests demonstrate correct construction contracts and preserved state
logic, not a live Amiga screen test.

## Short Amiga smoke test

Open Account settings. An unchecked control should be empty and a checked
control should display the native checkmark. Toggle periodic retrieval,
verify its interval chooser, switch accounts, save and reopen. Also verify
shared SMTP credentials and Save sent mail in the system-folder dialog.
Cancel a change once to check that it does not persist. Existing settings do
not need to be recreated.

## API basis

Original classic SDK headers (not included in this patch):

- `gadgets/checkbox.h` 44.1: `CHECKBOX_Checked` aliases `GA_Selected`.
  https://github.com/BartmanAbyss/vscode-amiga-debug/blob/055097bba74dd1b2f764dcb90781d2017bd1d499/bin/linux/opt/m68k-amiga-elf/sys-include/gadgets/checkbox.h
- `proto/checkbox.h`: declares `CheckBoxBase` for `CHECKBOX_GetClass()`.
  https://github.com/BartmanAbyss/vscode-amiga-debug/blob/055097bba74dd1b2f764dcb90781d2017bd1d499/bin/linux/opt/m68k-amiga-elf/sys-include/proto/checkbox.h
