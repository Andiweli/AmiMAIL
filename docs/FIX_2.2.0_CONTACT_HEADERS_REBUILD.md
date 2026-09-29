# AmiMAIL 2.2.0 - contact headers rebuilt from the original implementation

## Scope and baseline

The contact dialog is rebuilt from the original `gui_contacts.c` supplied in
`AmiMAIL-2.0.5.zip`, before the contact-arrow experiments. That contact source
was unchanged in the subsequently supplied functional updates before the
arrow work. This is a contact-module reset, not an application downgrade.

Only `src/gui_contacts.c` changes in the application. AmiMAIL remains 2.2.0,
the executable remains `bin/AmiMAIL`, and Catalog V12 is unchanged. The
separate scrollbar-width correction in `src/gui_transfer.c` is not reverted
or included in this patch.

## What was removed

All earlier contact header repainting, replacement header text drawing,
background filling and window post-refresh painting are removed. There is
no contact-header `RectFill()` or `SetFont()`. Original native dialog layout,
header text, frames and fonts remain the responsibility of ListBrowser.

The original contact-row render hook is retained. It additionally records
the actual cell bounds and font height supplied by the native gadget. It
does not change the header's font or draw any header text.

## New arrow and input implementation

Every native column has `LBCIA_Sortable`, `LBCIA_AutoSort` and
`LBCIA_SortArrow` disabled at creation. No native `LBM_SORT` operation or
`LISTBROWSER_SortColumn` setting is used. This removes the source of the
competing native sort indicator instead of trying to cover its pixels.

A private ListBrowser subclass supplies mouse activation and release handling
only for the First name and Last name headings. This is important: disabling
native sorting alone would make those headings unclickable on the affected
ListBrowser. The new input path is independent of the native sorting flags.
The native class still handles layout, painting, contact rows, multi-selection,
scrolling and column separator dragging. A click on the Email heading is
ignored. The original `LISTBROWSER_TitleClickable` styling is retained.

After the native painter has finished, the subclass adds only one 5x4 arrow
through the existing `gui_draw_sort_icon()` in `gui_icons.c`, also used by
the Date column. No title text or background is cleared or replaced.

Each successful mouse release carries its column in its own event code.
This prevents rapid queued clicks from overwriting a single pending-column
variable. Sorting happens in the dialog's normal event loop, not inside the
input-context dispatcher. Pressing, dragging away, Escape and right-button
cancellation do not sort. Native row releases are distinguished from those
private heading codes.

The list is detached before reordering. A stable, iterative merge sort
relinks the existing nodes without allocating copies or changing their
selected flags, IDs, strings or render hooks. Cursor and scroll position
are retained where the native ListBrowser permits it.

## Required behavior in both contact dialogs

| Action | Sort | Only visible arrow |
| --- | --- | --- |
| Open | First name A-Z | First name, down |
| Click First name again | First name Z-A | First name, up |
| Change to Last name | Last name A-Z | Last name, down |
| Click Last name again | Last name Z-A | Last name, up |
| Change back to First name | First name A-Z | First name, down |
| Click Email address | No change | No email arrow |

After editing, importing or deleting contacts, the active name column and
its current sort direction are retained for the rebuilt list. The next
opening of either dialog starts with First name A-Z.

## Tests actually executed

- Existing host regression executable: 506 checks, zero failures.
- Contact regression test with GCC and Clang, warnings treated as errors.
- Contact tests also run with AddressSanitizer and UndefinedBehaviorSanitizer
  under both compilers.
- All 256 eight-click First/Last sequences, with sorting only on release.
- Column changes reset to A-Z; repeated active-column clicks reverse order.
- Aborted clicks, rapid queued clicks, divider/body input delegation,
  native-release code collisions, font metrics and changed cell geometry.
- Empty lists, email-only recipient selection, rebuilding after edits,
  class allocation failure and class/resource cleanup.
- Sorting of 1,024 existing nodes, preserving node selection and identity.
- Paint-contract tests reject modifications to simulated native title pixels
  other than the seven opaque pixels of the existing sort-arrow mask.
- The complete Amiga-only branch of `gui_contacts.c`, including both dialog
  constructors/event loops, syntax-checked with GCC and Clang using explicit
  host API declarations and the project's real internal headers.
- Confirmed `gui_transfer.c` is byte-identical to the last X-button-width fix.

These checks use API test doubles. They are not an Amiga emulator and do not
prove the exact behavior of a particular native ListBrowser version. No
m68k compiler, native link test or AmigaOS visual/input run was available.
In particular, the paint tests verify what AmiMAIL itself draws; they do
not emulate the native class's internal rendering.

Reproduce the included checks from the source directory:

```sh
make host-test
python3 tests/test_contact_sort_icons.py
python3 tests/check_contacts_native_syntax.py
HOST_CC=clang python3 tests/check_contacts_native_syntax.py
HOST_CC=clang CONTACT_ICON_TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' python3 tests/test_contact_sort_icons.py
```

The complete-file syntax check uses the previously supplied
`tests/check_native_syntax.py` for the common API declarations. Neither
syntax checker should be mistaken for the actual Amiga NDK.

## Installation and Amiga verification

Replace `src/gui_contacts.c` and the included test files in the current
2.2.0 source tree. Run `make`, replace the Amiga executable with the resulting
`bin/AmiMAIL`, and restart AmiMAIL. No catalog replacement is needed.

Verify the table above in Contact management and in the recipient selector.
Also drag both column separators, resize the window, uncover it after another
window has covered it, and check contact selection, editing and importing.
Header fonts and bevels should look as they did before the arrow experiments.
