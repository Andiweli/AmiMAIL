# AmiMAIL 2.2.0 — Safe composer closing and ordered confirmation construction

## Baseline and scope

Built on the supplied 2.2.0 requester-diagnostics patch, on top of the accepted
native checkboxes, Reply/delete fix, contact headers with the icons one pixel
lower, and status-bar cancel-width fix. Those unrelated components are not
replaced by this patch. All modified source files are supplied in full.

Version stays **2.2.0**, output **bin/AmiMAIL**, catalog **V13**. No new messages
or catalog version are introduced. The German catalog from the previous patch
remains the correct one.

## Confirmed defects

The previous composer event loop set `done = 1` directly for Escape and the
window close gadget. Only the Cancel button asked about saving. Closing the
application while a composer was active could bypass the question as well.
The inner event loop also continued processing queued events after setting
`done`, allowing later events in the same batch to change the first decision.

The shared confirmation returned the same Boolean false for explicit No,
Escape, closing the question, and construction errors. The preceding patch
separated errors through `confirmation_failure` but not a dismissed question
from explicit permission to discard a draft.

These source-level defects are fixed. They do not establish why the requester
sometimes fails to open on the affected Amiga. No requester log or R-code from
that specific failure was supplied in this turn.

## Composer behaviour

Escape, the Cancel button, the composer close gadget, and application quit
requests while composing all go through one close-confirmation path.

- Yes queues the current draft through the existing draft-save implementation.
  The composer closes only if the queue operation succeeds. A later network
  failure is still handled by the existing transfer/recovery implementation;
  local queue acceptance must not be mistaken for a server acknowledgement.
- Explicit No closes the composer without saving. When editing a server draft,
  this continues to leave the existing server draft unchanged.
- Escape in the question, the question's close gadget, or Ctrl-C during the
  question dismisses it and keeps the composer open. None authorizes discard.
- Construction or queue failure keeps text and temporary attachments in the
  composer. Requester failures now show their R-code in the compose status line
  as well, instead of replacing it with a generic message.

As with the previous Cancel-button behaviour, the question is not conditional
on a new dirty-state heuristic: it is consistently offered for all close paths.
This avoids relying only on a body-change flag and missing subject, recipient,
attachment or seeded-reply changes.

Escape inside the editable TextEditor produces a single verified gadget event.
It does not invoke the native editor's potential edit/revert action, open a
window from an input hook, or export text from input.device context. Copy,
paste, selection, navigation and repeated Return retain their existing paths.
The read-only mail preview is not modified.

The triggering compose input batch is drained before opening the confirmation.
Repeated close events in that batch coalesce. Events after the first successful
Send cannot send again. Repeated Escape key-downs are ignored by the question.

If the main application was being closed and the user chooses Yes, AmiMAIL's
main window stays running so the existing asynchronous save can complete.
Immediately stopping the network worker would cancel that queued operation.
If the user explicitly chooses No on that quit path, quitting may proceed.
Dismissal/failure cancels the quit attempt. This does not implement a new
application-wide wait-for-all-transfers shutdown policy.

## Requester construction and lifetime

The shared Yes/No requester still uses native button.gadget, layout.gadget and
window.class. The accepted label/button sizes, layout relationships, native
rendering and pre-open centering calculation are retained.

The constructor is now an ordered series of `NewObjectA()` calls using the
already-open libraries' class pointers. No public `"button.gadget"` lookup or
nested variadic constructor expression is used in this path. Each component
is checked before it is placed in its parent. An absent optional note does not
create an ignored, unattached object.

Ownership is explicit through `CHILD_NoDispose`: parent groups are disposed
before their retained children. The root layout is attached to the window only
after the window constructor succeeds, matching the existing attachment-dialog
ownership convention. Successful, failed and repeated constructions use the
same cleanup function. This eliminates silent child allocation failures and
ambiguous cleanup in this path; it is not evidence of the original trigger on
the affected machine.

A new detailed result distinguishes Yes, No, dismissal and error. Existing
mail/contact deletion callers retain the old **true only for Yes** wrapper.
No positive error result or negative error value can authorize deletion.

## Diagnostics

The previous tracing mechanism remains available:

```text
SetEnv AmiMAIL_RequesterDebug 1
```

The log is **T:AmiMAIL-requesters.log**; save it before rebooting. Existing
R1–R5 meanings are unchanged. With tracing enabled, `requester.build-step`
identifies the last construction step on failure:

1. question text object;
2. optional explanatory text;
3. Yes button;
4. No button;
5. button row;
6. root layout;
7. window object;
8. window opening;
9. window signal mask.

Step zero indicates missing required class pointers. `requester.classes`
reports which pointers were available. Additional `compose.close-question`,
`compose.draft-queue`, `compose.discard-confirmed` and `compose.kept-open`
records describe the close decision without logging mail contents.
`requester.result` retains its Boolean Yes result in v1; v2 now contains the
detailed outcome (0 dismissed, 1 Yes, 2 No).

Disable verbose logging after the test:

```text
SetEnv AmiMAIL_RequesterDebug 0
```

## Verification

Performed against the reconstructed, supplied baseline and patched tree:

- Full existing `make -j3 review-test` passed before editing.
- Full updated `make -j3 review-test` passed with GCC.
- `make host-check` passed (non-Amiga branches only).
- Requester tests execute the actual constructor, cleanup, logger, delete
  gates and composer close helper against explicit native API doubles.
- Failures are injected at every component allocation, missing class pointer,
  window opening and signal-mask stage. Accepted children are disposed by the
  simulated failing constructor to test ownership, not merely skipped.
- 128 successive constructions with and without a note leave no live test
  objects. Successful construction after a failed attempt is covered.
- Separate tests execute the actual composer event loop and editor dispatcher:
  new/reply/forward/edit-draft modes; Cancel, Escape, close gadget, verified
  editor Escape, menu Quit, main Quit and Ctrl-C; all outcomes; duplicate queued
  actions; repeated Escape; queue failure followed by retry; iconified-main
  fallback; no premature network shutdown after queueing a draft.
- Both focused suites passed with GCC and Clang, `-Werror`, AddressSanitizer
  and UndefinedBehaviorSanitizer. These suites use native API doubles, not
  Amiga libraries. They cannot reproduce all Intuition behaviour.
- The final package was reapplied to a fresh copy of the baseline and checked.
- The catalog binary and unrelated GUI source files remain byte-identical.

**Not performed:** a real m68k link/build, AmigaOS GUI/input test, live IMAP
save, or reproduction of the original post-Gloom requester failure. The
constructor change is defensive hardening, not a proven diagnosis of that
external interaction.

## Installation and targeted Amiga test

Replace/add the files in this package, then run:

```text
make clean && make
```

On the Amiga replace only **AmiMAIL** with the new **bin/AmiMAIL**. Keep the
previous German V13 catalog. No account, folder, window or spool settings need
to be deleted.

Start a new mail, change its body/subject and attach a test file. Check Escape,
Cancel and the close gadget separately. Dismiss the resulting question: the
composer must remain open with its current content. Try closing again and
choose Yes; verify the saved draft in the Drafts folder. Repeat without
restarting AmiMAIL. Explicit No must still discard the local edits. Repeat
with Reply and with an edited existing draft. Check one mail-delete requester
between composer tests.

If opening fails, note the displayed **R-code** and retain the trace from that
run; neither a generic “confirmation failed” message nor available RAM alone
identifies the failing native call.

## Native API references

- layout.gadget, CHILD_NoDispose and child ownership:
  https://developer.amigaos3.net/autodocs/layout.gadget/
- BOOPSI GM_HANDLEINPUT, GMR_VERIFY and gpi_Termination:
  https://wiki.amigaos.net/wiki/BOOPSI_Gadgets
