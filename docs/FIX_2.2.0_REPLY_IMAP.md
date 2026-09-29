# AmiMAIL 2.2.0: Reply popup and server-side Delete

Date: 2026-09-28

## Baseline and scope

This update is based on the last confirmed AmiMAIL 2.1.0 build including
Herald subjects, per-account retrieval intervals and the remembered attachment
export directory. It does not reintroduce any experimental preview selection
or AmigaOS 3.1/ClassAct code. Only full changed/new files are included.

The program and package version are 2.2.0. The executable remains
`bin/AmiMAIL`. The catalog is generation 12, with 569 string IDs. The four new
IMAP status/error messages have English built-ins and German translations.
The actual compiled German catalog is included, not just its sources.

The read-only preview, status/progress geometry, account ordering, notification
sounds, Herald configuration, retrieval intervals and attachment workflow
have not been redesigned. The configuration format is unchanged; no reset is
required. The existing 20 MiB combined attachment limit is unchanged.

## Reply popup: close on release

Previously the temporary popup window was disposed when it became inactive.
Clicking its main-window arrow activates the main window on mouse-down, so the
popup disappeared before mouse-up. The old suppression flag prevented reopening
but only delayed the arrow-state update, not disposal of the popup itself.

The popup now transfers ownership to the main GUI when that activation comes
from the Reply arrow. It stays visible until the main button's `GADGETUP`.
A small observer in the existing button subclass records native activation and
inactivation, including a cancelled drag that never produces `GADGETUP`.
It only records state and signals the GUI; it never disposes objects or waits
from the input context. The GUI drains queued button events before the cancelled
activation cleanup, preventing the closing click from reopening the popup.

The two popup labels have static backing storage because `button.gadget`
retains the text pointers and the popup can now outlive its creating call.
Teardown, iconification and unrelated context changes safely release the retained
window. Escape and ordinary outside clicks still dismiss the popup. No mouse
polling loop, artificial delay, new gadget library or preview changes were added.

## IMAP Delete: what was found

A concrete defect was present in capability negotiation: AmiMAIL requested
CAPABILITY before authentication (and again after STARTTLS), but did not refresh
it after LOGIN/SASL. Servers are allowed to expose a different capability set
once authenticated. This could cause AmiMAIL to overlook available MOVE or
UIDPLUS support and take the safe mark-only fallback unnecessarily.

A successful LOGIN/SASL is now followed by an explicit CAPABILITY request.
The authentication parser does not currently retain an automatic CAPABILITY
response code, so the explicit query is necessary. PREAUTH connections already
query capabilities in the authenticated state and do not perform a redundant
LOGIN. An unsuccessful post-login query is reported as a connection failure
instead of continuing with stale capabilities.

Capability recognition uses complete atoms from untagged CAPABILITY responses,
not substring matches in arbitrary server prose. IMAP4rev2's core MOVE/UIDPLUS
semantics are recognized as well.

A fragmented-greeting regression test also exposed early acceptance of the
prefix `* OK` before the greeting line had finished arriving. AmiMAIL now waits
for the complete greeting line before sending its first command, with the
existing maximum-line limit applied.

These are findings in the supplied source and scripted protocol tests. There
was no transcript from the user's live server, so they do not establish the
exact cause of every provider-specific deletion symptom.

## Delete behavior and safety

**Delete still means move into the server's mapped Trash folder.** It is not
permanent erasure. Empty Trash remains the distinct permanent-removal action.

For each selected source UID, AmiMAIL now:

1. Selects the intended source mailbox if necessary and checks
   `UID FETCH <uid> (UID FLAGS)` to confirm that the target exists.
2. Uses `UID MOVE` when supported; otherwise uses `UID COPY`,
   `UID STORE +FLAGS.SILENT (\Deleted)` and, when available, `UID EXPUNGE`.
3. Rechecks the source UID before claiming completed removal.

The two verification commands retrieve flags/UID only, never a message body.
They add two small server round trips per selected message.

A tagged OK can be returned for a nonexistent UID without changing anything.
The preflight check catches that situation. Failed copy/store/move/expunge
commands are not shown as successful removals. A failed verification after an
accepted mutation produces an uncertainty message; the client does not blindly
repeat the copy or move. Reload before retrying, since the destination may
already contain a copy.

If the server really supports neither MOVE nor UIDPLUS, the safe fallback is
still a destination copy with the source marked deleted. AmiMAIL now explicitly
reports that deferred deletion instead of simply saying the message was moved.
The source row can be hidden by AmiMAIL while other clients still show a deleted
message until the server expunges it. A mailbox-wide EXPUNGE is intentionally
not used for an individual Delete: it could permanently remove unrelated mail
marked deleted by another client. This update does not promise immediate removal
on servers lacking both extensions.

Additional protections:

- Source mailbox, Trash destination, network/account object and selected UIDs
  are captured before the confirmation dialog. Nested event processing cannot
  redirect a confirmed Delete to a later account/folder selection.
- A late successful Delete/Move response cannot remove a same-numbered UID from
  another folder currently displayed in the GUI.
- Mailbox names remain case-sensitive except the special INBOX name.
- Selecting the already stored mailbox name no longer risks overlapping string
  source/destination buffers.
- The Delete/Move worker cases verify/re-establish the IMAP connection before
  submitting the operation, but do not automatically replay a failed mutation.

This is not a general rewrite of draft cleanup or provider-specific Trash
policies. Existing explicit Empty Trash semantics remain unchanged.

## Installation

1. Back up the working source tree and current installed executable/catalog.
2. Overlay the contents of the patch ZIP on the last confirmed 2.1.0 source tree.
3. Run `make clean && make` with the existing m68k toolchain and SDK settings.
4. Quit AmiMAIL on the Amiga and install:
   - `bin/AmiMAIL` as the program `AmiMAIL`;
   - `_Catalogs/deutsch/AmiMAIL.catalog` as
     `Catalogs/deutsch/AmiMAIL.catalog` in the installed program drawer.
5. Copy the updated English/German guides to their existing guide locations.

The build checks and regenerates the German catalog. Do not install only the
new executable and keep a stale catalog: the new error messages need V12.
The program itself does not require an English catalog.

## Verification performed

The baseline was reconstructed from the confirmed patch chain and its latest
file versions were byte-compared with the supplied archives. The final patch
is compared against that baseline so unchanged files are not included.

Host validation performed in this environment:

- `make -j4 review-test host-check`: passed. This runs the existing regression,
  file replacement, MIME/mailfile, attachment export, streaming SMTP/IMAP,
  progress, catalog, locale, sound/account-order, Herald and interval suites.
- New `make imap-mutation-test`: 11,450 checks, zero failures, against the actual
  `src/imap.c` with a command-gated scripted TLS peer. Coverage includes native
  MOVE, UIDPLUS fallback, deferred deletion, failures at each command, nonexistent
  and unrelated UIDs, case-sensitive folders, quoted names, verification failure,
  and deliberately fragmented server reads.
- The same suite covers post-LOGIN, SASL-IR, STARTTLS and PREAUTH negotiation,
  post-authentication capability failure, and an end-to-end move that becomes
  available only after login. Greeting/reply chunks of 1 and 7 bytes are tested.
- New `make reply-popup-test`: 271 checks, zero failures. It extracts the actual
  popup/arrow implementation and GUI Delete/Move handlers into an explicit
  ReAction/Exec API-double harness. It checks held clicks, normal release, rapid
  click/release, cancelled drags, outside click, Escape, popup actions, allocation
  failures, teardown, immutable confirmation context and late-folder replies.
- Both new suites passed with GCC and Clang and with AddressSanitizer plus
  UndefinedBehaviorSanitizer (including stack-use-after-return checks).
  Changed code is compiled with the project warnings and `-Werror`.
  For Clang only, the unchanged `src/i18n.c` translation unit uses
  `-Wno-format-nonliteral`, since it intentionally applies a validated catalog
  format string. No such exception is applied to changed protocol/UI code.
- German catalog source/ID/format/binary validation: generation 12, 569 IDs.

These counts include repeated cases/fragmented I/O assertions; they are not
counts of distinct end-user scenarios.

**Not performed here:** a complete m68k build, real Intuition/ReAction rendering
or input scheduling, an AmigaOS emulator/hardware run, or live mail-provider
verification. API doubles are not a substitute for those checks.

## Suggested Amiga checks

- Open the Reply menu, press its arrow and keep holding: the popup must remain
  visible. Release: it must close once without reopening. Also try rapid clicks,
  dragging away before release, Escape, Reply All and Forward.
- Delete one disposable test mail, then inspect both its original folder and
  the provider's Trash in another client/webmail. Reload the original folder.
- Repeat with multiple test mails. Change displayed folders while the server
  is replying; an unrelated row with the same UID must not vanish.
- Confirm the configured Trash mapping is the correct server folder.
- On a server without MOVE/UIDPLUS, expect the explicit deferred-deletion message
  rather than a dangerous automatic mailbox-wide expunge.
- Recheck ordinary connection/login, STARTTLS if used, fetch, Herald notification,
  status progress and the unchanged read-only preview.

## Protocol references

- RFC 9051, authentication state/capability changes and UID/MOVE semantics:
  https://www.rfc-editor.org/rfc/rfc9051.html
- RFC 4315, targeted UID EXPUNGE and unrelated deleted-message safety:
  https://www.rfc-editor.org/rfc/rfc4315.html
- RFC 6851, MOVE extension:
  https://www.rfc-editor.org/rfc/rfc6851.html
