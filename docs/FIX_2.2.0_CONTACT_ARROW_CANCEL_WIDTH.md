# AmiMAIL 2.2.0 - contact heading cleanup and cancel-button width

## Scope and baseline

The contact source is based on the delivered contact-header-click-fix.
The progress source is the latest delivered status-height-folder-progress
version (later patches did not replace src/gui_transfer.c).

Only two production files change: src/gui_contacts.c and src/gui_transfer.c.
Version 2.2.0, output bin/AmiMAIL and catalog generation 12 stay unchanged.
No new user-visible text, catalog ID, native class or build dependency is added.

## Contact headings

The user confirmed that sorting and the new arrow masks now work, but a
persistent additional native down arrow remains in First name. The existing
code already passes SortArrow=FALSE and SortColumn=-1. The exact internal
cause in the installed ListBrowser was not verified with a native debugger.

This correction is a DISPLAY-ONLY compatibility workaround: the existing
post-refresh overlay repaints the content interiors of the two name headings,
then draws only the application-owned active 5x4 arrow. It does not remove or
patch the operating system's internal rendering code. Native title hit targets,
column separators and outer frames remain native. The background pen comes
from the native DrawInfo; captions use the current font and existing catalog
strings, clipped to the available width. No fixed palette colour is used.

The same overlay is called after sort clicks, column adjustments, list rebuilds,
window refreshes and processing the dialog's input queue. First name and Last
name keep LBCIA_Sortable=TRUE; Email stays FALSE. AutoSort and SortArrow stay
FALSE for all three columns. No input hook or extra gadget is introduced.

The working contacts_columns(), sorter, click handler, state transitions,
selection and cursor handling are byte-identical to the previous source.

- Opening: First name A-Z, custom down arrow only in First name.
- Repeating the active heading: reverse order and arrow.
- Switching name column: A-Z in the new column, old heading has no arrow.
- Email: not sortable, no custom arrow.
- Applies to Contacts and the recipient selector.

## Cancel width

The X button previously had a fixed minimum of 18 pixels. Its minimum and
maximum widths now use the native minimum domain of the preview scrollbar
immediately above it, measured with LayoutLimits before opening the window.
That scrollbar is already a zero-weight child in the preview layout, so its
natural minimum is also its allocated width. X has zero width weight and
cannot absorb surplus horizontal space. The common status-row height is not
changed, and no relayout is triggered by a progress update.

If the native class fails to report a positive minimum, an available actual
scrollbar width is used, then the existing GUI_SCROLLBAR_WIDTH fallback.
The screen's ordinary font/theme settings are measured at construction time;
no claim is made about live replacement of system gadget classes or themes.

## Verification performed

1. Targeted contact tests with GCC and Clang, -Werror, ASan and UBSan.
   They retain the previous name-click gates, all 256 eight-click sequences,
   stable sorts up to 10,000 rows, selection/cursor retention, rebuilds,
   empty books, recipient filtering and resize-bound checks.
2. The pixel double now deliberately paints an extra old first-column arrow
   independently of the flags. The previous test never painted it and could
   therefore miss the reported visual problem. The new test verifies its
   removal for both active headings and directions, plus nonzero background
   pens, fonts 8..20 pixels high, and untouched pixels outside title interiors.
3. Dialog/progress tests with GCC and Clang, -Werror, ASan and UBSan.
   352 combinations cover scrollbar widths 10..40 pixels, status heights
   10..30 pixels, native/fallback gauges, equal min/max X width and zero width
   weight. Existing export, directory persistence, cancellation, progress
   context and constructor-failure tests also pass.
4. Negative controls: disabling the heading cleanup produces a runtime pixel
   assertion failure; restoring a fixed 18-pixel X width produces a runtime
   width assertion failure. These are not just source-pattern checks.
5. Existing native-branch syntax check passed with its explicit API doubles.

Commands:

    python3 tests/test_contact_sort_icons.py
    python3 tests/test_dialog_regressions.py
    python3 tests/check_native_syntax.py

Repeat the first two with HOST_CC=clang or gcc and the corresponding
CONTACT_ICON_TEST_FLAGS / DIALOG_TEST_FLAGS set to:

    -fsanitize=address,undefined -fno-omit-frame-pointer

IMPORTANT: These are host C tests with API and drawing doubles. They do NOT
emulate the installed ReAction renderer, verify its actual header geometry,
or replace an AmigaOS display test. No full m68k build or hardware/UI test
was performed. The complete unrelated project suite was not rerun.

## Installation

Copy src/gui_contacts.c and src/gui_transfer.c into the current source tree.
Run make. Quit AmiMAIL on the Amiga and replace the executable with the new
bin/AmiMAIL. No catalog, icon, guide, Makefile or other runtime file changes.
The archive also contains the full updated tests and this report.

On the Amiga check both contact windows: the old First-name arrow must stay
absent after opening, column switches, repeated clicks, column resizing,
scrolling, contact editing and window covering/uncovering. Check the captions
and bevels visually. The X button should match the preview scrollbar width,
including after resizing the main window; progress cancellation is unchanged.

## API references

https://developer.amigaos3.net/autodocs/listbrowser.gadget/
https://developer.amigaos3.net/autodocs/listbrowser.gadget/SetLBColumnInfoAttrsA.html

These document the separation between Sortable, SortArrow, AutoSort and the
native title-click gate. The compatibility paint does not change those flags.
