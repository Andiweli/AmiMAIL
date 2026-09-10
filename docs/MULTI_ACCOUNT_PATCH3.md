# Multi-account patch 3

This maintenance patch addresses the remaining UI and character-set details
found while testing the multi-account configuration.

## Main-window tab strip

The lower account selector remains the native ReAction `clicktab.gadget`. Its
backfill is now set to `LAYERS_NOBACKFILL`, so the strip does not paint a second
light-gray rectangle. The main content area uses the screen's standard ReAction
background pen, so the native tab bevels and their corner pixels blend into the
surrounding window. The header and About banner retain the new `#999999`
artwork background. The optional layout-side inset is also disabled for this
unattached tab bar.

AmigaOS 3.2 still does not implement clicktab's documented flipped
orientation. The tabs therefore remain docked at the lower edge using the
supported ReAction layout.

## German text encoding

The locked-account validation message is stored as UTF-8 because it is placed
in `AmgError` and displayed through `set_utf8_string()`. The German catalog now
uses the UTF-8 byte sequence for `Ändern`, avoiding a second, incorrect charset
interpretation. Local-only labels and status strings retain the native Amiga
single-byte encoding.

## Unlock button semantics

`Unlock` is now enabled only if all of the following are true:

- an encrypted account file exists;
- the selected account has no usable secret in the current draft.

It is needed for an older/keyless account, or after its cached session and
persistent keys were removed. Accounts opened automatically from a cached key
show the button disabled. A successful manual unlock also disables it
immediately.

## Verification

- portable regression suite: 328 checks, 0 failures;
- host warning-check compilation;
- AmigaOS 3.2 NDK syntax checks for changed C files;
- German catalog regenerated as an IFF CTLG file.
