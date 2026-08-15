# AmiMail architecture

## Goal

AmiMail is a native single-account IMAP/SMTP mail client for AmigaOS 3.2+.
It retains the proven AmiGmail separation between the ReAction GUI and a
blocking network worker while keeping provider-specific behaviour isolated from
the generic mail protocol path.

```text
ReAction GUI
    |  AmgNetMessage / Exec message ports
    v
Network worker
    |-- IMAP -------- AmiSSL/TLS ---- configured IMAP server
    |-- SMTP -------- AmiSSL/TLS ---- configured SMTP server
    `-- optional provider-specific OAuth code
```

## GUI / worker split

The GUI task owns all ReAction objects, list state, compose windows, previews and
requesters. Network operations are submitted to the separate worker via Exec
message ports so blocking DNS, TCP, TLS, IMAP and SMTP activity does not run in
the GUI task.

AmiMail intentionally has no persistent local message cache. Message lists and
payloads are read live from IMAP and held in RAM only as needed.

## Account model

`AmgAccount` stores independent IMAP and SMTP endpoints, ports, STARTTLS flags
and login names. An empty protocol-specific login falls back to the configured
email address. SMTP can use its own password or fall back to the IMAP password.

The client is intentionally single-account. Multi-account support is not part
of the design target.

## Transport security

Both secure transport styles are supported:

- implicit/direct TLS, typically IMAP 993 and SMTP 465
- STARTTLS upgrade on an existing TCP socket, typically IMAP 143 and SMTP 587

STARTTLS is capability checked before credentials are sent. After a successful
upgrade, IMAP refreshes `CAPABILITY` and SMTP sends a fresh `EHLO` before
authentication.

Transport reads and writes use bounded readiness waits and explicit timeout
errors so a silent server cannot leave the worker waiting indefinitely.

## IMAP authentication

Password accounts support:

- `LOGIN` over the already protected TLS channel when permitted
- `AUTHENTICATE PLAIN` with SASL initial response (`SASL-IR`)
- `AUTHENTICATE PLAIN` with the classic continuation exchange

`PREAUTH` greetings are handled without a redundant login. A PREAUTH greeting
before an explicitly requested STARTTLS upgrade is rejected rather than
continuing with an unsafe transport state.

The retained Google XOAUTH2/PKCE implementation is provider-specific and is not
required by the normal account GUI.

## SMTP authentication

SMTP capabilities are taken from the final `EHLO` response (after STARTTLS when
used). Password authentication supports `AUTH PLAIN`, including a `334`
continuation, and `AUTH LOGIN` fallback when advertised/required.

A successful SMTP delivery is kept distinct from later IMAP bookkeeping. For
example, if an optional Sent-copy APPEND fails after SMTP already accepted the
message, AmiMail reports a warning rather than a false send failure that might
encourage duplicate delivery.

## Mailbox discovery and system roles

Mailbox discovery uses:

1. `CAPABILITY`
2. `LIST "" "*" RETURN (SPECIAL-USE)` when `SPECIAL-USE` is advertised
3. normal `LIST "" "*"` fallback
4. legacy `XLIST "" "*"` compatibility fallback

Standard roles include Sent, Drafts, All, Junk/Spam, Trash and Flagged.
`\Noselect` containers remain in the hierarchy for their children but cannot be
opened as message folders or used as move targets.

The account settings also allow manual role mappings for servers that do not
provide reliable Special-Use metadata. Automatic detection remains active for
roles left blank.

## Message operations

Normal list FETCH requests use standard IMAP data only; Gmail-only `X-GM-*`
items are not required.

Move/delete behaviour is safety-oriented:

- use `UID MOVE` when available
- otherwise use COPY + `\\Deleted` + `UID EXPUNGE` when UIDPLUS is available
- without MOVE/UIDPLUS, do not issue a mailbox-wide EXPUNGE merely to finish a
  single-message move

Deferred `\\Deleted` records are hidden from subsequent AmiMail lists. Full
EXPUNGE remains intentional when the user explicitly empties Trash/Junk.

## Drafts and Sent copies

Drafts are stored with IMAP `APPEND` and can be reopened for editing. To/CC/BCC,
subject, body, attachments and reply references are restored. Re-saving an
edited draft appends the replacement first and removes the old UID only after
that succeeds. Sending an edited draft removes the original only after SMTP
success.

For non-Google servers AmiMail can optionally APPEND a private copy of a
successfully sent message to the resolved Sent folder. Gmail/Googlemail hosts
are skipped because their SMTP service already maintains Sent Mail.

## Persistence and credential security

AmiMail uses separate paths from AmiGmail:

```text
ENVARC:AmiMail/account.cfg
ENVARC:AmiMail/folders.state
ENV:AmiMail.session-key
```

New account files use `AMIMAIL-ACCOUNT-2`. Secrets are encrypted with
AES-256-GCM using a PBKDF2-HMAC-SHA256-derived key. The master password itself
is never persisted. A derived 256-bit unlock key may be cached only in volatile
`ENV:` for the current AmigaOS session.

Legacy `AMIMAIL-ACCOUNT-1` files are migrated once: the old stored master is
used only to decrypt and immediately rewrite the account as ACCOUNT-2.

## Provider-specific compatibility

Generic IMAP/SMTP code does not depend on Gmail extensions. Gmail-specific
handling is limited to compatibility behaviour such as app-password whitespace
normalisation, known transport settings, the legacy XLIST fallback and avoiding
duplicate Sent APPENDs.

Optional Google OAuth code remains isolated in `oauth.c` and related config
headers.

## GUI module layout in RC2

The former monolithic GUI unit is split into private implementation modules:

```text
gui.c             lifecycle/shared GUI helpers
gui_dialogs.c     account/unlock/system-folder/about/confirm dialogs
gui_compose.c     new/reply/draft/mailto-seeded composer
gui_contacts.c    local contacts UI
gui_folders.c     mailbox tree and expansion state
gui_messages.c    message list/flags/selection
gui_preview.c     preview, URLs and received attachments
gui_window.c      main ReAction window/layout/render hooks
gui_actions.c     controller/actions/network events
gui_runtime.c     Wait()/timer/iconify/mailto event loop
gui_state.c       window state and ENV mail status
gui_update.c      update UI state
gui_notify.c      DataTypes notification playback
gui_mailto.c      mailto-to-compose bridge
```

`src/gui_internal.h` is private to those modules; `include/gui.h` remains the
small public GUI API.

## Iconify and runtime

ReAction `WINDOW_AppPort`/`WM_ICONIFY`/`WMHI_UNICONIFY` are used instead of
pretending that iconify is application shutdown. While iconified the Intuition
window pointer may be NULL, but the network worker, periodic timer, ENV status,
notification signal and mailto hand-off port remain alive.

A stale `timer.device` signal is never treated as proof that the currently
referenced IORequest has completed. The event loop checks `CheckIO()` before
`WaitIO()`.

## Contacts

Contacts are local provider-independent data in `ENVARC:AmiMail/contacts.dat`.
They do not depend on the IMAP account being unlocked and do not use the network
worker. CSV/VCF parsing and duplicate detection live outside the compose module;
Compose only calls the contact-selection UI.

## mailto / single-instance hand-off

`mailto.c` parses RFC-6068-style URI data and owns the lightweight IPC/startup
layer. The public Exec port is:

```text
AMIMAIL.MAILTO
```

When AmiMail is already running, a second mailto invocation transfers the URI
to that port and exits instead of establishing another mail session. If no
instance is running, the short browser-launched process stores the URI in a
private `T:AmiMailMailto.*` file and starts the real instance asynchronously.

The normal GUI event loop consumes the port signal. If AmiMail is iconified it
is first uniconified; `gui_mailto.c` then converts decoded UTF-8 fields to the
local GUI charset and opens Compose in `COMPOSE_MODE_NEW`. Mailto data therefore
cannot accidentally acquire draft UID/replacement semantics.

## Update architecture

The GitHub release check runs in the existing network worker, never in the GUI
task. The repository endpoint is `Andiweli/AmiMAIL`; the expected release asset
schema is `AmiMAIL-<tag>.lha`. Download is user-triggered and goes to `RAM:`;
AmiMail does not auto-install or unpack releases.
