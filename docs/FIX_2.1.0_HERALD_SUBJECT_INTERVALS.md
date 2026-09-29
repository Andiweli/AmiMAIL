# AmiMAIL 2.1.0: Herald subjects and per-account retrieval intervals

Patch date: 2026-09-28. Program version remains **2.1.0**; executable output
remains **bin/AmiMAIL**. German catalog generation is **11**.

## Baseline and scope

This patch is incremental to the last Herald-notifications patch, including
all preceding 2.1.0 stability, attachment-directory, sound/account-order,
status-height/folder-progress and locale corrections. The archive contains
complete changed/new files, not a replacement of the entire source tree.

No conversation view or new preview editor is included. Sound playback,
attachment export and its remembered directory, the message list and the
existing context-scoped status progress are unchanged.

## Changes

### Herald subjects

New-mail cards always include the new-message count, account label and subject.
There is deliberately no separate subject-visibility setting. Examples:

```text
1 new message - Private
Subject: Appointment confirmation

3 new messages - Private
Latest: Appointment confirmation
```

With the German catalog, the corresponding prefixes are `Betreff:` and
`Neueste:`. Missing/blank subjects display `(No subject)` / `(Kein Betreff)`.
The `?` test button still sends a separate test card, not a simulated new mail.

The selected subject belongs to the highest new, non-deleted Inbox UID above
the previous notification baseline. This represents newest arrival, not the
sender-supplied Date header. Out-of-order FETCH records and deleted messages
are handled. The baseline is captured before the new high-water mark is saved.
The same logic receives actual header data for both active and background
accounts. It does not request bodies or additional headers from the server.

The existing RFC 2047 decoder and UTF-8-to-local converter are reused. Controls,
line breaks, backslashes and repeated whitespace from the account/subject text
are neutralized before constructing the card. The native Herald command
builder retains its existing quote/star escaping. A trusted literal `\n`
separates the two lines, as specified by Herald's API. Long account labels
are limited to 32 display bytes; subjects use the remaining space in Herald's
160-byte TEXT budget, with `...` when shortened. This budget is checked before
command quoting. German umlauts remain single-byte local text.

Herald remains optional and disabled by default. Its per-slot IDs/groups,
NOSOUND setting, asynchronous transport, coalescing, error handling and test
routing have not changed. Subjects are now intentionally visible outside
AmiMAIL whenever new-mail Herald notifications are enabled.

### Configuration layout

The following left-hand labels are added without changing the dialog's
pre-open centering or corrected lower border calculation:

| English | German |
| --- | --- |
| Email retrieval: | Mail-Abruf: |
| Notifications: | Benachrichtigungen: |
| External: | Extern: |

The shared left label column is measured using the screen font, with its
previous minimum width preserved. The Herald button is now `?`, with the same
32-pixel minimum/maximum width and zero horizontal weight as the `...` button.
It remains a test action, not a help action.

The periodic checkbox label no longer includes a fixed minute value. A native
ReAction chooser in popup/cycle mode sits at the right of that row and offers
1, 2, 5, 10, 15 and 30 minutes. It is ghosted unless periodic retrieval is
checked. Turning the checkbox off retains the choice. Account switches and
reordering collect and restore the choice for the same stable account slot.

### Scheduling and persistence

`periodic_fetch_minutes` is stored separately in each account configuration.
Only the six offered values are accepted. Missing/invalid values use five
minutes, matching the old fixed timer. Old installations therefore do not
suddenly begin checking every minute. Existing credentials and Herald/sound
settings use their unchanged persistence paths.

A small scheduler maintains separate deadlines for the five stable account
slots. The existing timer.device request wakes for the earliest due deadline;
only due, enabled, unlocked accounts are queued. Unchanged account settings
retain their deadlines on a timer restart. Changing an interval or enabling
periodic retrieval starts a fresh interval for that account only.

The scheduler uses monotonic E-Clock time, not adjustable wall-clock time.
Unsigned 32-bit deadline arithmetic is wrap-safe. Modal windows or slow workers
can delay a check. Missed intervals are not replayed as a burst, and an already
pending periodic connect/check is not queued again. Startup retrieval remains
independent. The normal serialized per-account IMAP worker is unchanged.

## Installation

1. Back up the current sources and configuration. Overlay this archive onto
   the source tree containing the previous Herald patch.
2. Run `make clean && make`. A clean rebuild is required because account and
   private GUI structures and notification function signatures changed.
3. Close AmiMAIL on the Amiga. Replace its executable with `bin/AmiMAIL`.
4. Replace `Catalogs/deutsch/AmiMAIL.catalog` with the binary from
   `_Catalogs/deutsch/AmiMAIL.catalog` in this patch. It is the actual generated
   V11 catalog, not just a revised CD/CT source. It contains 565 entries.
5. Copy the updated German/English guides as appropriate and restart AmiMAIL.

No settings-file editing, RexxMast, HeraldSend installation or version-number
change is needed. chooser.gadget V45+ is opened alongside the existing ReAction
classes and closed after the window objects have been disposed. It is the
classic ReAction chooser, not an OS4-only or MUI gadget.

## Verification performed

Tools: GCC 14.2.0, Clang 17.0.0, Python 3.13.5 on the Linux host.

- The baseline host regression suite passed before applying this patch.
- `make -j4 review-test host-check` passed on the patched tree. This includes
  the existing message/MIME, streaming, storage-failure, attachment-directory,
  dialog, sound/account-order and status-progress suites.
- Subject selection, charset decoding, shortening, independent schedules,
  deadline wrap, persistence and legacy/malformed interval tests:
  **36,456 checks** passed.
- Production configuration-row construction, collect/show and checkbox logic,
  actual timer helper/completion code and due-account queue dispatch:
  **643 checks** passed using explicit ReAction/Exec/timer/network API doubles.
- The production Herald transport and GUI bridge with real header decoding:
  **83,573 checks** passed using Exec/rexxsyslib/timer doubles. These include
  EN/DE one-/multiple-message cards, quoted/star-containing subjects, highest
  new UID selection and maximum-size 160-byte text. Existing Herald settings
  round-trip/migration tests also passed (**68 checks**).
- The new retrieval tests and Herald suites passed with both GCC and Clang,
  and with AddressSanitizer plus UndefinedBehaviorSanitizer enabled.
  GCC sanitizer runs use `-fno-pie -no-pie`; the Clang-only exception for
  validated nonliteral formats is confined to unchanged `src/i18n.c`.
- Catalog build and 31 catalog-tool tests passed: V11, 565 matched entries,
  713 registered static lookup uses and 26 audited fallback IDs.
- The native chooser's documented attributes and `ReadEClock` calling contract
  were checked against the AmigaOS 3 autodocs listed below.

These are **host and API-double tests**, not a native build or emulated Amiga.
There was no real m68k compiler/link test, actual ReAction pixel/keyboard test,
real timer.device/Herald server run or live mailserver integration test here.
The host settings test does not exercise AmiSSL encryption. An API double
cannot prove native ABI compatibility or screen layout behavior.

### Reproduce the focused checks

```sh
make catalogs-check herald-test herald-retrieval-test
HOST_CC=clang python3 tests/test_herald_retrieval.py
HOST_CC=clang python3 tests/test_herald.py
RETRIEVAL_TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' make herald-retrieval-test
HERALD_TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' make herald-test
```

### First Amiga checks

- Check the three left labels and matching `?` / `...` widths with the normal
  font. Verify no extra blank row reappears below Save/Cancel.
- Toggle periodic retrieval off/on and check that the right-hand chooser
  changes enabled state immediately. Try all six choices, switch accounts,
  reorder them and cancel once; then save and restart.
- Set two accounts to one and two minutes and compare the automatic Inbox
  checks. Check that only their own interval applies. Restore preferred values.
- Receive one test message, then a batch of several, including a long encoded
  subject. Check the count, account and latest subject on the Herald card.
- With Herald stopped, automatic notifications should remain silent; `?`
  should report that Herald is not running. The previous progress display,
  sound, read-only preview and attachment-directory behavior should be intact.

## References and existing limits

- AmigaOS 3 chooser.gadget attributes and popup/cycle semantics:
  https://developer.amigaos3.net/autodocs/chooser.gadget/
- AmigaOS 3 timer.device/ReadEClock signature and E-Clock frequency:
  https://developer.amigaos3.net/autodocs/timer.device/ReadEClock.html
- Supplied Herald `Developer/Herald-ARexx-API.md`: TEXT length, literal `\n`,
  command quoting and notification ID behavior.
- `docs/FIX_2.1.0_HERALD.md` remains the report for the original transport.
  Its former description of cards without subjects and a `Test` label is
  superseded by this document. Its bounded shutdown/quarantine limitation for
  a Herald that never replies is unchanged; the transport was not rewritten.
