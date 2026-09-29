# AmiMAIL 2.2.0 - restore contact header clicks

## Correction to the previous patch

The previous active-column patch disabled LBCIA_Sortable for First name and
Last name while leaving LISTBROWSER_TitleClickable enabled on the gadget.
On the user's ListBrowser this stopped the two headings from accepting
clicks. Disabling a sort arrow must not disable the column's click gate.
The previous report's claim that TitleClickable alone was sufficient was
incorrect for the reported system.

Both name columns now use LBCIA_Sortable=TRUE. Email keeps it FALSE.
All three columns keep LBCIA_AutoSort=FALSE and LBCIA_SortArrow=FALSE.
LISTBROWSER_SortColumn remains -1 in both dialogs and on every reattach.
The redundant blanket LBCIA_Flags assignments were removed. Separator
resizing remains enabled by LBCIA_DraggableSeparator=TRUE.
This is the same tag separation used by the working mail Date heading.
No native LBM_SORT call, new API, new class or input hook was added.

Only contacts_columns() and its explanatory comment changed in production.
Everything after it is byte-identical to the previous active-sort patch:
the stable sorter, single active arrow state, 5x4 masks, cell-bound tracking,
selection/cursor preservation, contact editing and both window layouts.

## Retained behavior

- Default: First name A-Z; only First name shows the custom down arrow.
- Repeated click on the active name column: toggle A-Z/down and Z-A/up.
- Switch name column: start A-Z/down; remove the old column's arrow.
- Email address: no sort action and no arrow.
- Both contact management and recipient selection use this behavior.
- The application version stays 2.2.0; output stays bin/AmiMAIL.
- No catalog changes: continue using the existing V12 catalog.

## Why the previous host test missed the regression

The old fixture injected LBRE_TITLECLICK directly into the handler, even
when the generated column flags would not allow that event. It tested the
sort handler, not whether a user could reach it.

The updated fixture obtains each dialog's TitleClickable setting from the
production source and checks the per-column Sortable/CIF_SORTABLE flags
before generating a title-release event. It checks that the two name
columns are reachable and that the email column is not. Defensive handling
of an unexpected email-column event is tested separately.

## Verification performed for this correction

- Targeted test: python3 tests/test_contact_sort_icons.py, GCC with -Werror.
- The same test: GCC with AddressSanitizer and UndefinedBehaviorSanitizer.
- The same test: Clang with AddressSanitizer and UndefinedBehaviorSanitizer.
- Both dialog click gates, disabled gates, email rejection and state changes.
- All 256 eight-click name-column sequences (2,048 state transitions).
- Stable sorting for 0 through 10,000 nodes, both keys and both directions.
- Selection/list-link integrity, cursor/top preservation, rebuilds, empty
  books, recipient filtering and independent dialog state.
- Existing 5x4 mask/pixel requests, single active arrow, column resize bounds
  and narrow-column guards.
- Six negative controls were rejected without source-wiring assertions:
  the previous delivered column configuration, each name gate disabled,
  email made sortable, and each dialog's TitleClickable disabled.
- Source integrity check confirms that code after contacts_columns() is
  byte-identical to the delivered active-sort baseline.

These are targeted HOST tests with API/graphics doubles, not a ListBrowser
emulator. No full m68k build, real AmigaOS input test or native arrow-rendering
test was performed. The full project's unrelated regression suite was not
rerun for this small correction.

## Installation

Replace src/gui_contacts.c and run make. Quit AmiMAIL on the Amiga and
replace its executable with the rebuilt bin/AmiMAIL, then start it again.
No catalog, icon, guide, Makefile or other production file needs replacement.
The ZIP additionally contains the updated test and this corrected report.

On the Amiga, open Contacts and try First name -> Last name -> Last name ->
First name. Expected: First name Z-A/up -> Last name A-Z/down -> Last name
Z-A/up -> First name A-Z/down. Repeat in recipient selection. Check that only
the active name heading has a custom arrow and Email stays non-sortable.

## Reference

Public ReAction documentation separates Sortable, AutoSort and SortArrow:
https://developer.amigaos3.net/autodocs/listbrowser.gadget/SetLBColumnInfoAttrsA.html

TitleClickable, title events and SortColumn are documented here:
https://developer.amigaos3.net/autodocs/listbrowser.gadget/

## Source hashes

Previous src/gui_contacts.c SHA-256:
f014b0c0cdd772358b2234e854bc6948bf1e94391638fe3ee6232078dd634efa

Corrected src/gui_contacts.c SHA-256:
ac902263bbad9a275ee07de83c76c0303c0593f99dcf4f7cdf12662bfb87e665
