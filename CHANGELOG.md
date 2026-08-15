# Changelog

## AmiMail 1.0 RC2 - 2026-08-15 - Public release candidate 2

- promote the field-tested AmiGmail-1.6 alignment P2 code to AmiMail 1.0 RC2
- add `mailto:` support for To/CC/BCC/Subject/Body with RFC-6068-style percent decoding and header CR/LF neutralisation
- add browser-friendly detached `mailto:` startup so an external browser command does not stay blocked while AmiMail remains open
- add the public Exec hand-off port `AMIMAIL.MAILTO`; a mailto launch forwards to the already-running AmiMail instance instead of starting a second mail session
- uniconify AmiMail on an incoming mailto request and explicitly bring the compose window to front/activate it
- preserve AmiMail ACCOUNT-2/session-key startup and unlock behaviour while integrating the single-instance mailto hand-off
- retain P2 features: modular GUI, iconify/background fetch, embedded AppIcon, notifications, contacts/import/multi-delete and GitHub update UI
- finish classic-NDK warning cleanup at the remaining DOS `GetVar()` boundary and use the warning-clean AmiGmail 1.6 DOS/Exec patterns in the new mailto module
- shorten the account-dialog STARTTLS hint to a single compact line suitable for low-resolution Amiga screen modes
- fix notification sound completion ordering so a stale preview-completion signal cannot immediately dispose the first new-mail sound
- finish the DE/EN audit for mailto parser/startup errors and the remaining ReAction window creation error
- keep the m68k `GetVar()` buffer explicitly typed as `STRPTR`, addressing the final signedness warnings reported by the real NDK build
- update the embedded program identification, `$VER:` string, Makefile package version and visible header/About version to 1.0 RC2
- expand portable regression coverage to 283 checks with dedicated mailto parsing/startup tests
- add final `docs/MAILTO.md`, `docs/UPDATE.md`, RC2 release notes and release test matrix

## AmiMail 1.0 RC1 - AmiGmail 1.6 Alignment Patch 1 - 2026-08-15

- first structural/runtime alignment pass based on the final tested AmiGmail 1.6/v60 architecture, without reintroducing Gmail-only assumptions
- split the former monolithic `gui.c` into private GUI modules for dialogs, compose, folders, messages, preview, window construction, actions, runtime/event loop, state, notifications and contacts; `gui.c` now remains the small lifecycle/shared-helper shell
- add a private `src/gui_internal.h` while keeping `include/gui.h` as the small public API
- preserve AmiMail-specific generic IMAP/SMTP, STARTTLS, manual system-folder mapping, sent-copy, draft editing and ACCOUNT-2/session-key behavior
- add native ReAction iconify with Workbench AppPort/AppIcon and continued worker/timer/ENV/sound operation while iconified
- embed the existing AmiMail Workbench icon as the fixed iconify resource; no external iconify `.info` file is required
- add versioned main-window geometry persistence (`AMIMAIL-WINDOW-2`)
- add live `AmiMAILStatus` ENV/ENVARC state tracking based on the Inbox unread count
- add optional DataTypes notification sound with ASL file selection/preview and one-sound-per-new-fetch-batch logic
- add local Contacts/Address Book with seven fields, safe persistence, CSV/VCF import, conservative duplicate detection and To/CC/BCC compose integration
- port the safe contacts editor focus pattern using `ActivateLayoutGadget()` so ReAction TAB traversal stays under layout.gadget control
- split local configuration preferences from network settings and add asynchronous `AMG_NET_RECONFIGURE` instead of synchronously stopping/restarting the worker from the modal account dialog
- harden the 5-minute timer against stale signals by checking `CheckIO()` before event-loop `WaitIO()` and only restarting the timer when the periodic-fetch toggle actually changes
- move URL opening out of the TextEditor hook and into the normal GUI event loop through a dedicated signal
- add iconified-safe ListBrowser/TextEditor state updates so background fetches can update the model even when `gui->window == NULL`
- carry over the AmiGmail 1.6 classic-GCC hook/warning patterns where applicable (`__typeof__` Hook assignments and 32-bit tag pointer casts)
- expand portable tests to 232 checks, including contacts/import and notification preference persistence
- deliberately defer the optional GitHub updater and `mailto:`/single-instance port until AmiMail-specific repository/asset/startup conventions are defined

## AmiMail 1.0 RC1 - 2026-08-14 - Release Candidate 1

- promoted the tested 0.1.27 code base to the first AmiMail 1.0 release candidate
- no functional protocol/UI changes relative to 0.1.27 Patch 15
- confirmed real-world Gmail operation with IMAP 993 direct TLS and SMTP 587 STARTTLS
- confirmed successful IMAP access against a second private non-Google provider
- retained the single-account design by intent
- documented the remaining real-server coverage targets: IMAP 143 STARTTLS, manual system-folder mapping, and non-Google Sent-copy APPEND
- synchronized source, executable identity, Makefile packaging version and release documentation

## AmiMail 0.1.27 - 2026-08-14 - Patch 15: status-bar charset cleanup

- audited every direct `status_local()` message in the AmigaOS GUI
- fixed the remaining German status texts that accidentally used UTF-8 bytes on the local Amiga text path
- corrected `für`, `verfügbar` and `geöffnet` in folder availability/container status messages
- audited all `amg_tr_snprintf()` formats used by the bottom status bar; no further UTF-8/local-charset mixups remain
- left the network/error status path unchanged because it intentionally carries UTF-8 and converts through `status_utf8()` before display
- no IMAP, SMTP, folder, compose, draft, account or requester behavior changed

## AmiMail 0.1.26 - 2026-08-14 - Patch 14: final cleanup / RC preparation

- completed a release-candidate cleanup pass without adding new user-facing features
- corrected the generated Message-ID fallback domain from `amigmail.local` to `amimail.local`
- fixed a corrupted German Gmail STARTTLS string (`für`)
- refreshed architecture/OAuth documentation to match the implemented STARTTLS, auth, folder, draft and ACCOUNT-2 behaviour
- removed obsolete patch-history comments from runtime GUI code
- added explicit pointer-to-ULONG casts for ListBrowser render-hook tags used by classic 32-bit ReAction varargs
- added classic-NDK compatibility casts for timer/socket calls that previously produced avoidable signedness/const warnings
- cleaned the portable test harness so GCC `-fanalyzer` no longer reports the account-file buffer as potentially uninitialised
- kept the known-good startup unlock requester geometry/focus and the final account-window centring unchanged

## AmiMail 0.1.25 - 2026-08-14 - Patch 13: protocol hardening

- distinguish IMAP `PREAUTH` from a normal `OK` greeting and skip redundant authentication on secure PREAUTH sessions
- reject PREAUTH-before-STARTTLS as an unsafe/ambiguous transport combination
- add IMAP `AUTHENTICATE PLAIN` continuation support for servers without `SASL-IR`
- add a continuation-aware IMAP SASL transaction so error challenges cannot leave the worker waiting for a tagged result that the server is withholding
- allow LOGIN-to-PLAIN fallback when PLAIN is advertised, while preserving network/timeout errors instead of relabelling them as credential failures
- parse SMTP AUTH mechanisms from the final EHLO response, including `AUTH=...` compatibility syntax
- add SMTP AUTH PLAIN `334` continuation handling and retain AUTH LOGIN fallback when advertised
- complete SMTP XOAUTH2 error challenges so the server can return its final authentication status
- reject overlong SMTP response lines explicitly
- retain each connection's configured timeout and use bounded socket-readiness waits for plain TCP and TLS I/O
- expand portable regression coverage from 186 to 198 checks
- retain Patch 12N account-window centring; its purely cosmetic first-frame reposition is deferred to the later GUI refactoring/polish pass

## AmiMail 0.1.24 - 2026-08-14 - Patch 12N: definitive account-window centring

- account window is now centred from its actual post-layout Intuition Width/Height
- final position is calculated relative to the current AmiMail main window and clamped to screen bounds
- `WPOS_CENTERWINDOW` remains only as an initial placement hint
- removed the unsuccessful fixed-width workaround from Patch 12M
- restored the STARTTLS/Gmail status hint
- startup password requester is untouched

## AmiMail 0.1.23 - 2026-08-14 - Patch 12M: deterministic account-window centering

- fixed Account settings to a deterministic width so ReAction cannot enlarge the initial requester after its position has been calculated
- Account settings use `WINDOW_RefWindow` + `WPOS_CENTERWINDOW` relative to the AmiMail main window
- initial status line starts empty so the long STARTTLS/Gmail hint cannot alter the initial layout width
- runtime status/error messages still use the same status gadget
- no post-open `MoveWindow()` is used, so the dialog does not visibly jump
- startup unlock requester/focus and all mail/account logic are unchanged

## AmiMail 0.1.22 - 2026-08-14 - Patch 12L: account-window centering

- restored the proven AmiGmail account-dialog positioning behavior
- account settings now use `WPOS_CENTERSCREEN`, matching the stable AmiGmail v32 implementation
- removed the AmiMail-only `WPOS_CENTERWINDOW` positioning that was visibly offset on the tested AmigaOS 3.2 setup
- unlock requester, session-key handling, IMAP/SMTP, drafts and all other UI logic are unchanged

## AmiMail 0.1.21 - 2026-08-14 - Patch 12K: restore proven startup focus path

- traced the focus regression to Patch 12F, where post-open `MoveWindow()` centering was introduced
- removed post-open requester movement from the startup unlock window
- removed the later `WINDOW_Activate`, delay-loop and `WMHI_ACTIVE` focus workarounds
- restored the exact focus sequence that worked on the real Amiga before the regression: `RA_OpenWindow()` followed immediately by `ActivateGadget()` on the password StringObject
- keeps the compact shrink-wrapped requester height, so the blank rows below the buttons do not return
- centres the fixed 460-pixel requester width before opening; vertical placement uses the compact 80-pixel natural-height hint and is never adjusted after opening
- preserves Return/Enter unlock, Escape cancel, ACCOUNT-2 encryption and volatile ENV session-key behaviour
- no IMAP, SMTP, draft, folder, message or account-data logic changed

## AmiMail 0.1.20 - 2026-08-14 - Patch 12J: force password field focus at startup

- activate the unlock window through ReAction `WINDOW_Activate` after `RA_OpenWindow()`
- wait a bounded number of timer ticks until Intuition reports `WFLG_WINDOWACTIVE`
- call `ActivateGadget()` directly on the master-password StringObject as soon as the window is active
- retain `WMHI_ACTIVE` as a fallback focus path
- keep requester geometry, centering, ENTER/RETURN handling, ESC handling and session-key logic unchanged

## AmiMail 0.1.19 - 2026-08-14 - Patch 12I: reliable startup password focus

- unlock requester now listens for `IDCMP_ACTIVEWINDOW`
- password field is activated on ReAction `WMHI_ACTIVE`, when the window is actually active
- removed the too-early `ActivateGadget()` call immediately after opening the requester
- preserved compact requester size, centring, RETURN/ENTER, ESC and session-key behaviour
- host regression suite remains at 186 checks with 0 failures

## AmiMail 0.1.18 - 2026-08-14 - Patch 12H: AmigaOS 3.2 focus build fix

- removed the unsupported `GA_Activate` tag from the startup unlock password StringObject
- keeps immediate startup focus via the existing Intuition calls `ActivateWindow()` + `ActivateGadget()` after `RA_OpenWindow()`
- fixes the real m68k AmigaOS 3.2 build error: `GA_Activate undeclared`
- preserves the compact requester height, centering, Return/Enter unlock, Escape cancel and ENV session-key behavior
- no account, IMAP, SMTP, draft, folder or message logic changed

## AmiMail 0.1.17 - 2026-08-14 - Patch 12G: startup focus + account-window centering

- restored immediate keyboard focus to the startup master-password field
- the unlock window explicitly re-activates itself after final positioning and then activates the password gadget
- attempted an additional creation-time focus tag; Patch 12H removes it because AmigaOS 3.2 NDK does not provide `GA_Activate`
- changed Account settings from screen-centering to `WINDOW_RefWindow` + `WPOS_CENTERWINDOW`, so it opens centered over the AmiMail main window
- preserved the compact Patch-12F unlock layout, Return/Enter submission, Escape cancel and session-key behavior
- no account encryption, IMAP, SMTP, draft, folder or message logic changed

## AmiMail 0.1.16 - 2026-08-14 - Patch 12F: clean unlock requester layout

- removed forced unlock-window height/min-height/max-height
- the startup unlock requester now derives its height exclusively from its shrink-wrapped ReAction contents
- removes the unused blank rows below the Unlock/Cancel buttons
- exact centring is recalculated immediately after ReAction has resolved the natural window height
- keeps the already working RETURN/ENTER, ESC and session-key behaviour unchanged

## AmiMail 0.1.15 - 2026-08-14 - Patch 12E: compact unlock requester

- reduced the fixed startup unlock-requester height introduced by Patch 12D
- removed the visible unused space below the Unlock/Cancel button row
- preserved the exact manual centering over the AmiMail main window
- preserved Return/Numpad-Enter to unlock and Escape to cancel
- no changes to account encryption, ENV session-key handling, IMAP or SMTP

## AmiMail 0.1.14 - 2026-08-14 - Patch 12D: exact unlock centering + Return fix

- replaced `WPOS_CENTERWINDOW` for the startup unlock requester with explicit geometry calculated from the already opened AmiMail main window
- requester width and height are fixed before opening, so horizontal and vertical centering are deterministic and no post-open move is required
- keeps the requester inside the public-screen bounds on smaller screens
- fixed Return/Enter submission from the `SHK_PASSWORD` string gadget on classic ReAction by accepting its observed zero termination code as well as carriage return
- Tab is still excluded from submission and continues normal gadget navigation
- Escape and mouse-button behavior remain unchanged
- ACCOUNT-2 encryption and volatile `ENV:AmiMail.session-key` handling are unchanged
- no IMAP, SMTP, drafts, folders or message behavior changed

## AmiMail 0.1.13 - 2026-08-14 - Patch 12C: unlock requester polish

- fixed startup unlock requester positioning by using its final fixed width before `WPOS_CENTERWINDOW` is evaluated
- requester now opens centered over the AmiMail main window without the previous right offset
- converted the directly displayed German unlock/requester/status strings back to Amiga-compatible single-byte umlaut escapes
- session-key caching and ACCOUNT-2 encryption logic are unchanged from Patch 12B
- no IMAP, SMTP, draft, folder or message behavior changed

## AmiMail 0.1.12 - 2026-08-14 - Patch 12B: one unlock per Amiga session

- retained secure `AMIMAIL-ACCOUNT-2` storage with no persisted master password
- added volatile `ENV:AmiMail.session-key` caching of the PBKDF2-derived 256-bit account key
- AmiMail now asks for the master password only once per AmigaOS session, not once per application launch
- cached session keys are bound to the current ACCOUNT-2 salt and stale keys are discarded automatically
- saving/re-encrypting account settings refreshes the volatile session key
- ACCOUNT-1 migration now also seeds the volatile session key after successful ACCOUNT-2 conversion
- unlock requester text now explains the per-Amiga-session behavior
- no IMAP/SMTP, draft, folder, message-list or compose behavior changed

## AmiMail 0.1.11 - 2026-08-14 - Patch 12: secure master-password storage

- introduced `AMIMAIL-ACCOUNT-2` for all newly saved account configurations
- removed reversible `remembered_master` storage from new account files
- encrypted IMAP/SMTP passwords and OAuth refresh tokens remain protected by AES-256-GCM with the master-password-derived key
- added one-time migration for legacy `AMIMAIL-ACCOUNT-1` files: the old stored master is used only to decrypt once, then the file is immediately rewritten as ACCOUNT-2 without it
- subsequent application starts show a compact password-only unlock requester centered over the AmiMail main window
- Return/numeric Enter unlocks; Escape cancels, consistent with the requester keyboard behavior introduced in Patch 10
- cancelling unlock leaves the account metadata visible but keeps network credentials locked until Account settings are used to unlock
- the Account settings master-password field is no longer prefilled from disk
- the legacy-master reader only accepts ACCOUNT-1 and remains solely for automatic migration
- added storage regression checks verifying ACCOUNT-2 output and absence of `remembered_master`
- host regression suite expanded to 186 checks

## AmiMail 0.1.10 - 2026-08-14 - Patch 11: editable IMAP drafts

- the main Reply button changes to `Bearbeiten` / `Edit` while the Drafts folder is active
- selected drafts can be reopened in the normal compose window
- restores To, CC, BCC, Subject and message body from the stored MIME draft
- restores `In-Reply-To` and `References`, so reply drafts keep their thread metadata
- extracts draft attachments to temporary `T:` files and restores them in the attachment list
- temporary draft attachment files are deleted on cancel/remove or by the network worker after a queued save/send
- keeps the existing draft `Message-ID` while generating a fresh Date for the next save/send
- saving an edited draft uses safe replace semantics: append the new draft first, then remove the former UID
- sending an edited draft removes the former draft only after SMTP delivery succeeded
- old-draft cleanup uses targeted UIDPLUS expunge when available and never falls back to a mailbox-wide `EXPUNGE`
- failures after a successful save/send are reported as warnings rather than inviting duplicate saves or sends
- an edited Drafts-folder list is refreshed after a clean replacement so the new UID appears immediately
- cancel/close without saving leaves the original server draft untouched
- Gmail, STARTTLS, message flagging, move/delete and requester-key behavior remain unchanged
- portable MIME round-trip coverage now verifies Bcc, reply references, body and attachment recovery from a generated draft
- host regression suite expanded to 179 checks

## AmiMail 0.1.9 - 2026-08-13 - Patch 10: safe IMAP delete/move + requester keys

- removed the unsafe plain `EXPUNGE` fallback after a single-message COPY+DELETE move when the server lacks UIDPLUS
- MOVE-capable servers still use `UID MOVE`; UIDPLUS servers still use targeted `UID EXPUNGE <uid>`
- servers with neither extension now leave the moved source record marked `\Deleted` instead of risking unrelated messages
- added `\Deleted` detection to parsed IMAP FETCH records
- message list rebuild/merge logic suppresses deferred-deleted records
- recent and incremental IMAP UID searches now include `NOT DELETED`
- selected mailbox `EXISTS` is decremented only when the source message was actually expunged/moved
- Empty Trash/Junk now restores the mailbox selected before the empty operation
- Return and numeric keypad Enter confirm the primary action in modal AmiMail requesters
- Escape keeps cancelling/closing requesters
- active account/system-folder string gadgets distinguish Return/Enter from Tab, so Tab continues field navigation rather than submitting
- compose behavior is deliberately unchanged
- host regression suite expanded to 161 checks with explicit `\Deleted` parser coverage

## AmiMail 0.1.8 - 2026-08-13 - Patch 9: IMAP Sent-copy support

- added optional IMAP `APPEND` of successfully sent messages to the resolved Sent folder
- sent copies are stored with `\Seen` so they do not appear as unread
- preserves Bcc in the private IMAP Sent copy while still omitting Bcc from SMTP DATA sent to recipients
- Gmail/Googlemail hosts are automatically excluded from the extra APPEND to avoid duplicate Sent messages
- added a `Gesendete Mails speichern` / `Save sent mail` checkbox to the System folders requester
- the setting is persisted as `save_sent_copy` in the backward-compatible `AMIMAIL-ACCOUNT-1` file
- existing account files without the new field default to sent-copy enabled for non-Google providers
- if SMTP delivery succeeds but the later IMAP APPEND fails, the send remains successful and AmiMail reports a warning instead of inviting an accidental resend
- automatic/manual Sent-folder resolution is reused from Patch 7
- single-account design remains unchanged
- host regression suite expanded to 159 checks

## AmiMail 0.1.7 - 2026-08-13 - Patch 8: system-folder requester polish

- fixed German Amiga character encoding in the Systemordner requester
- `Entwürfe` now uses the native single-byte `ü` representation instead of an UTF-8 literal
- `Übernehmen` now uses the native single-byte `Ü` representation instead of an UTF-8 literal
- Systemordner requester is now positioned relative to the AmiMail main window, not the account dialog
- no IMAP, SMTP, folder-mapping or storage logic changed
- single-account design remains unchanged

## AmiMail 0.1.6 - 2026-08-13 - Patch 7: manual system-folder mapping

- added optional manual mapping for Sent, Drafts, All Mail, Spam/Junk and Trash
- added compact `Systemordner: Zuordnen...` / `System folders: Map...` account-dialog subwindow
- empty mappings retain automatic `SPECIAL-USE` and name-based folder detection
- configured mappings override automatic server/name classification in the IMAP layer
- manual mappings are persisted in the existing `AMIMAIL-ACCOUNT-1` account file
- removed redundant GUI-side second-pass folder-name inference so overrides cannot be undone visually
- explicitly kept AmiMail as a single-account application; multi-account support is not planned
- host regression suite expanded to 151 checks

## AmiMail 0.1.5 - 2026-08-13 - Patch 6: SMTP STARTTLS send hang fix

- fixed the SMTP STARTTLS send path hanging indefinitely after the TLS upgrade
- root cause: `smtp_open()` already consumed the greeting and completed the final `EHLO`, while `smtp_authenticate()` incorrectly waited for a second SMTP greeting and sent another `EHLO`
- SMTP authentication now starts directly after the already completed final `EHLO`
- preserves the required SMTP sequence `greeting -> EHLO -> STARTTLS -> TLS -> EHLO -> AUTH`
- implicit/direct TLS SMTP continues through the same single greeting/EHLO path
- no folder, MIME, compose or IMAP behavior changed
- host regression suite remains clean at 141 checks

## AmiMail 0.1.4 - 2026-08-13 - Patch 5: Gmail STARTTLS guard

- fixed the apparent endless-connect case caused by configuring Gmail IMAP as port 143 + STARTTLS
- Gmail IMAP is now validated as direct TLS on port 993 before a network request starts
- Gmail SMTP is validated as either direct TLS on 465 or STARTTLS on 587
- account dialog help text now documents the Gmail exception explicitly
- preserves generic IMAP STARTTLS support for providers that actually advertise it
- host regression suite expanded to 141 checks

## AmiMail 0.1.3 - 2026-08-13 - Patch 4: IMAP/SMTP STARTTLS

- added plain TCP transport mode plus in-place AmiSSL TLS upgrade support
- added IMAP STARTTLS negotiation with pre-TLS and post-TLS `CAPABILITY`
- added SMTP STARTTLS negotiation with pre-TLS and post-TLS `EHLO`
- refuses to authenticate when STARTTLS was requested but the server does not advertise it
- added independent IMAP and SMTP STARTTLS checkboxes to the account dialog
- persisted STARTTLS flags remain in the existing `AMIMAIL-ACCOUNT-1` format
- kept direct/implicit TLS for IMAP 993 and SMTP 465 fully available
- improved TLS read/write handling so the common transport functions work before and after upgrade
- fixed the Makefile release version, which had remained at 0.1.0 in earlier AmiMail patches
- storage regression coverage now includes both STARTTLS flags
- host regression suite expanded to 136 checks

## AmiMail 0.1.2 - 2026-08-13 - Patch 3: background restore + asset rename

- restored the window background to the classic grey `#888888`
- fixed the regression where the new logo palette made the general window background white
- the main header strip now also uses the grey background outside the logo area
- renamed the logo asset from `assets/amigmail.png` to `assets/amimail.png`
- updated embedded-banner source comments to the new asset name
- preserved the Patch-2 Gmail authentication improvements and current Gmail compatibility

## AmiMail 0.1.1 - 2026-08-13 - Patch 2: banner + Gmail auth compatibility

- replaced the embedded header/About logo with the supplied AmiMail banner
- regenerated `src/banner_data.c` from the new 170x28 logo asset
- normalized account input before validation and save/load
- trims accidental surrounding whitespace from email, host and login fields
- automatically compacts Gmail-style app passwords entered as `xxxx xxxx xxxx xxxx`
- IMAP now prefers encrypted `LOGIN` and keeps `AUTHENTICATE PLAIN` as fallback when needed
- added regression coverage for account normalization and Gmail-style app-password compaction
- host regression suite expanded to 134 checks

## AmiMail 0.1.0 - 2026-08-13 - Foundation Patch 1

- forked the stable AmiGmail 1.1/v32 source base into AmiMail
- changed executable/project branding and persistent paths to AmiMail
- introduced the independent account format `AMIMAIL-ACCOUNT-1`
- added configurable IMAP and SMTP hosts, ports and user names
- added separate encrypted IMAP/SMTP password fields with SMTP-password fallback to IMAP
- replaced the Gmail-only 16-character app-password validation with generic password validation
- removed mandatory `X-GM-MSGID`, `X-GM-THRID` and `X-GM-LABELS` from normal message FETCH commands
- changed folder discovery to prefer RFC 6154 `SPECIAL-USE` when advertised
- retained normal `LIST` and Gmail `XLIST` as compatibility fallbacks
- added `\Noselect` parsing and protected non-selectable folder containers from opening/move operations
- changed internal system aliases to standard `\Flagged`, `\All` and `\Junk`, while retaining compatibility with `\Starred`, `\AllMail` and `\Spam`
- IMAP password authentication now uses the configured IMAP login and can fall back to `LOGIN`
- SMTP authentication now uses the configured SMTP login/password and falls back from `AUTH PLAIN` to `AUTH LOGIN`
- direct-TLS SMTP is no longer hardcoded to port 465; the configured direct-TLS port is used
- added an `imap_starttls` account flag alongside the existing SMTP flag for the next transport patch
- preserved the existing ReAction list/tree/preview/compose behavior and the v32 message-flag rendering
- portable regression suite expanded from 118 to 124 checks
