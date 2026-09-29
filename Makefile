PROJECT := AmiMAIL
VERSION := 2.2.0

ifeq ($(origin CC),default)
CC := m68k-amigaos-gcc
endif
HOST_CC ?= gcc
PYTHON ?= python3
HOST_TEST_FLAGS ?= -std=c99 -O2 $(COMMON_WARN) -Werror
LHA ?= lha
RELEASE_ASSET := AmiMAIL-v$(VERSION).lha
RELEASE_ICON := assets/Icons/AmiMail.info
RELEASE_CATALOGS := _Catalogs
RELEASE_README := $(wildcard README.md)
SOURCE_ROOT := $(notdir $(CURDIR))
ifeq ($(origin AR),default)
AR := m68k-amigaos-ar
endif

# Der Installationsprefix des Crosscompilers wird standardmaessig aus dem
# tatsaechlich gefundenen m68k-amigaos-gcc abgeleitet. Unter MSYS/UCRT64
# ergibt /c/amiga-gcc/bin/m68k-amigaos-gcc damit automatisch /c/amiga-gcc.
# Jeder Wert kann weiterhin explizit auf der make-Kommandozeile gesetzt werden.
CC_PATH := $(shell command -v $(CC) 2>/dev/null)
CC_PREFIX := $(shell if [ -n "$(CC_PATH)" ]; then dirname "$$(dirname "$(CC_PATH)")"; fi)

ifeq ($(origin AMIGA_PREFIX),undefined)
ifneq ($(strip $(CC_PREFIX)),)
AMIGA_PREFIX := $(CC_PREFIX)
else
AMIGA_PREFIX := /opt/amiga
endif
endif

NDK_INC ?= $(AMIGA_PREFIX)/m68k-amigaos/ndk-include
REACTION_SDK ?= $(NDK_INC)

# AMISSL_SDK must point at the AmiSSL Developer directory containing
# both include/ and lib/.  Respect an explicitly supplied value first.
# For the common AmiSSL 5 SDK archive layout, auto-detect the copy kept
# below the user's development tree before falling back to AMIGA_PREFIX.
AMISSL_SDK_HOME := $(HOME)/dev/amiga/sdk/AmiSSL-5.27-SDK/AmiSSL/Developer
AMISSL_SDK_PREFIX := $(AMIGA_PREFIX)/m68k-amigaos/amissl
ifeq ($(origin AMISSL_SDK),undefined)
ifneq ($(wildcard $(AMISSL_SDK_HOME)/include/proto/amissl.h),)
AMISSL_SDK := $(AMISSL_SDK_HOME)
else
AMISSL_SDK := $(AMISSL_SDK_PREFIX)
endif
endif
AMISSL_OS3_LIB ?= $(AMISSL_SDK)/lib/AmigaOS3

CPPFLAGS := -Iinclude -I"$(NDK_INC)" -I"$(REACTION_SDK)" -I"$(AMISSL_SDK)/include" -D__USE_INLINE__ -D__USE_BASETYPE__
COMMON_WARN := -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
AMIGA_CFLAGS := -m68020 -msoft-float -fomit-frame-pointer -fno-common $(COMMON_WARN)
AMIGA_LDFLAGS := -m68020 -msoft-float -L"$(AMISSL_OS3_LIB)"
# AmiSSL and bsdsocket are opened explicitly in src/tls.c.  Therefore the
# AmiSSL auto-open library must not be linked as well.  AMISSL_EXTRA_LIBS can
# be set to -lamisslstubs if a future source file passes AmiSSL entry points
# as function pointers.
AMISSL_EXTRA_LIBS ?=
AMIGA_LIBS := $(AMISSL_EXTRA_LIBS) -Wl,--start-group -lc -lstubs -lamiga -Wl,--end-group

SOURCES := src/main.c src/app.c src/splash.c src/common.c src/fileio.c src/transfer.c src/mailfile.c src/attachment_export.c src/buffer.c src/account.c src/periodic.c src/mail_notice.c src/codec.c \
           src/crypto.c src/imap_parser.c src/mime.c src/mailto.c src/oauth.c src/tls.c src/update.c \
           src/imap.c src/smtp.c src/storage.c src/contacts.c src/contacts_import.c \
           src/network_task.c src/gui.c src/gui_runtime.c src/gui_actions.c src/gui_mailto.c \
           src/gui_window.c src/gui_icons.c src/gui_update.c src/iconified_data.c src/gui_state.c src/gui_notify.c src/gui_herald.c src/herald.c \
           src/gui_dialogs.c src/gui_contacts.c src/gui_compose.c src/gui_folders.c \
           src/gui_messages.c src/gui_preview.c src/gui_attachments.c src/gui_transfer.c src/charset.c src/i18n.c src/banner_data.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

HOST_SOURCES := src/common.c src/fileio.c src/transfer.c src/mailfile.c src/attachment_export.c src/buffer.c src/account.c src/periodic.c src/mail_notice.c src/codec.c src/crypto.c \
                src/imap_parser.c src/mime.c src/mailto.c src/oauth.c src/tls.c src/smtp.c \
                src/storage.c src/contacts.c src/contacts_import.c src/update.c src/i18n.c src/charset.c src/herald.c
HOST_TEST := build/host-tests
HOST_HEADERS := $(wildcard include/*.h)
REVIEW_TEST := build/review-tests
FILEIO_TEST := build/fileio-fault-tests
SMTP_STREAM_TEST := build/smtp-stream-tests
CATALOG_TOOL := tools/catalog_tool.py

.PHONY: all release debug clean dist release-lha source-dist host-test host-check check-env catalogs catalogs-check catalog-test review-test mailfile-test imap-file-test smtp-file-test native-syntax-test mailfile-parity-test dialog-test progress-context-test imap-folder-progress-test locale-test

all: release

release: CFLAGS := $(AMIGA_CFLAGS) -Os -DNDEBUG
release: check-env catalogs bin/$(PROJECT)

debug: CFLAGS := $(AMIGA_CFLAGS) -O0 -g3
debug: clean catalogs bin/$(PROJECT)

bin/$(PROJECT): $(OBJECTS) | bin

	$(CC) $(AMIGA_LDFLAGS) -o $@ $(OBJECTS) $(AMIGA_LIBS)

build/%.o: src/%.c | build

	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build bin:

	mkdir -p $@

host-test: $(HOST_TEST)

	./$(HOST_TEST)

host-check: | build

	$(HOST_CC) -std=c99 -O2 $(COMMON_WARN) -Iinclude src/*.c -o build/amimail-host-check

$(HOST_TEST): tests/test_main.c $(HOST_SOURCES) $(HOST_HEADERS) | build

	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_main.c $(HOST_SOURCES) -o $@

$(REVIEW_TEST): tests/test_review.c $(HOST_SOURCES) $(HOST_HEADERS) | build

	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_review.c $(HOST_SOURCES) -o $@

$(FILEIO_TEST): tests/test_fileio_faults.c src/fileio.c $(HOST_HEADERS) | build

	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_fileio_faults.c -o $@

$(SMTP_STREAM_TEST): tests/test_smtp_stream.c $(HOST_SOURCES) $(HOST_HEADERS) | build

	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_smtp_stream.c $(filter-out src/smtp.c,$(HOST_SOURCES)) -o $@

# The shipped catalog is rebuilt deterministically from the same ID set used
# by the executable. Python is needed on the build host only, not the Amiga.
catalogs:

	@command -v $(PYTHON) >/dev/null || { echo "MISSING: $(PYTHON) (catalog build; set PYTHON=python if needed)"; exit 1; }
	$(PYTHON) $(CATALOG_TOOL) --build

catalogs-check:

	$(PYTHON) $(CATALOG_TOOL) --check

catalog-test:

	$(PYTHON) -m unittest discover -s tests -p test_catalog_tool.py

review-test: host-test $(REVIEW_TEST) $(FILEIO_TEST) $(SMTP_STREAM_TEST) catalogs-check catalog-test mailfile-test imap-file-test smtp-file-test native-syntax-test mailfile-parity-test dialog-test progress-context-test imap-folder-progress-test locale-test

	./$(REVIEW_TEST)
	./$(FILEIO_TEST)
	./$(SMTP_STREAM_TEST)

check-env:

	@command -v $(CC) >/dev/null || { echo "FEHLT: $(CC)"; exit 1; }

	@test -d "$(NDK_INC)" || { echo "FEHLT: NDK_INC=$(NDK_INC)"; echo "Compiler gefunden unter: $(CC_PATH)"; echo "Abgeleiteter AMIGA_PREFIX: $(AMIGA_PREFIX)"; echo "Falls noetig: make NDK_INC=/c/amiga-gcc/m68k-amigaos/ndk-include"; exit 1; }

	@test -d "$(AMISSL_SDK)" || { echo "FEHLT: AMISSL_SDK=$(AMISSL_SDK)"; exit 1; }

	@test -d "$(AMISSL_OS3_LIB)" || { echo "FEHLT: AMISSL_OS3_LIB=$(AMISSL_OS3_LIB)"; exit 1; }

dist: release

	rm -rf dist/$(PROJECT)-$(VERSION)

	mkdir -p dist/$(PROJECT)-$(VERSION)/docs

	cp bin/$(PROJECT) CHANGELOG.md LICENSE $(RELEASE_README) dist/$(PROJECT)-$(VERSION)/

	cp $(RELEASE_ICON) dist/$(PROJECT)-$(VERSION)/$(PROJECT).info

	cp docs/ARCHITECTURE.md docs/MAILTO.md docs/UPDATE.md docs/OAUTH_SETUP.md \
	   docs/FIX_2.1.0_HERALD.md docs/FIX_2.1.0_HERALD_SUBJECT_INTERVALS.md \
	   docs/HERALD_CLIENT_LICENSE.txt dist/$(PROJECT)-$(VERSION)/docs/

	cp -R config dist/$(PROJECT)-$(VERSION)/

	mkdir -p dist/$(PROJECT)-$(VERSION)/Catalogs/deutsch

	cp $(RELEASE_CATALOGS)/deutsch/AmiMAIL.catalog dist/$(PROJECT)-$(VERSION)/Catalogs/deutsch/

	cp -R _Guides dist/$(PROJECT)-$(VERSION)/Guides

	cd dist && tar -czf $(PROJECT)-$(VERSION)-AmigaOS3.tar.gz $(PROJECT)-$(VERSION)

release-lha: release

	@command -v $(LHA) >/dev/null || { echo "FEHLT: $(LHA) (fuer das GitHub-LHA)"; exit 1; }

	rm -rf dist/$(PROJECT)-$(VERSION) dist/$(RELEASE_ASSET)

	mkdir -p dist/$(PROJECT)-$(VERSION)/docs

	cp bin/$(PROJECT) CHANGELOG.md LICENSE $(RELEASE_README) dist/$(PROJECT)-$(VERSION)/

	cp $(RELEASE_ICON) dist/$(PROJECT)-$(VERSION)/$(PROJECT).info

	cp docs/ARCHITECTURE.md docs/MAILTO.md docs/UPDATE.md docs/OAUTH_SETUP.md \
	   docs/FIX_2.1.0_HERALD.md docs/FIX_2.1.0_HERALD_SUBJECT_INTERVALS.md \
	   docs/HERALD_CLIENT_LICENSE.txt dist/$(PROJECT)-$(VERSION)/docs/

	cp -R config dist/$(PROJECT)-$(VERSION)/

	mkdir -p dist/$(PROJECT)-$(VERSION)/Catalogs/deutsch

	cp $(RELEASE_CATALOGS)/deutsch/AmiMAIL.catalog dist/$(PROJECT)-$(VERSION)/Catalogs/deutsch/

	cp -R _Guides dist/$(PROJECT)-$(VERSION)/Guides

	cd dist && $(LHA) a $(RELEASE_ASSET) $(PROJECT)-$(VERSION)

	@echo "GitHub release asset: dist/$(RELEASE_ASSET)"

source-dist:

	mkdir -p dist

	rm -f dist/$(PROJECT)-$(VERSION)-source.zip

	cd .. && zip -qr "$(CURDIR)/dist/$(PROJECT)-$(VERSION)-source.zip" "$(SOURCE_ROOT)" \
		-x '$(SOURCE_ROOT)/build/*' '$(SOURCE_ROOT)/bin/*' '$(SOURCE_ROOT)/dist/*' '*.bak'

clean:

	rm -rf build bin dist/$(PROJECT)-$(VERSION) dist/$(PROJECT)-$(VERSION)-AmigaOS3.tar.gz dist/$(RELEASE_ASSET) dist/$(PROJECT)-$(VERSION)-source.zip

-include $(OBJECTS:.o=.d)

# File-backed MIME / selective export, including a full 20 MiB round trip.
build/mailfile-tests: tests/test_mailfile.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_mailfile.c $(HOST_SOURCES) -o $@

mailfile-test: build/mailfile-tests
	./build/mailfile-tests

build/imap-file-tests: tests/test_imap_file.c tests/transfer_tls_double.h src/imap.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_imap_file.c $(HOST_SOURCES) -o $@

imap-file-test: build/imap-file-tests
	./build/imap-file-tests

build/smtp-file-tests: tests/test_smtp_file.c tests/transfer_tls_double.h src/smtp.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_smtp_file.c $(filter-out src/smtp.c,$(HOST_SOURCES)) -o $@

smtp-file-test: build/smtp-file-tests
	./build/smtp-file-tests

native-syntax-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/check_native_syntax.py

build/mailfile-parity-tests: tests/test_mailfile_parity.c tests/test_main.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_mailfile_parity.c $(HOST_SOURCES) -o $@

mailfile-parity-test: build/mailfile-parity-tests
	./build/mailfile-parity-tests

# Explicit API doubles: these do not replace a real m68k/AmigaOS test.
dialog-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_dialog_regressions.py

progress-context-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_progress_context.py

# Actual IMAP parser with a scripted, command-gated TLS peer (host only).
build/imap-folder-progress-tests: tests/test_imap_folder_progress.c tests/transfer_tls_double.h src/imap.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_imap_folder_progress.c $(HOST_SOURCES) -o $@

imap-folder-progress-test: build/imap-folder-progress-tests
	./build/imap-folder-progress-tests

# Actual HTML/codec code with catalog-backed locale test doubles.
locale-test: catalogs-check
	HOST_CC="$(HOST_CC)" HOST_TEST_FLAGS="$(HOST_TEST_FLAGS)" $(PYTHON) tests/test_locale_markers.py

# One-click account reordering and the native sound worker lifecycle.
.PHONY: notify-order-test
notify-order-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_notify_order.py

review-test: notify-order-test

# Native asynchronous Herald client, configuration and notification routing.
.PHONY: herald-test
herald-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_herald.py

review-test: herald-test

# Per-account schedules and newest-subject selection/encoding.
.PHONY: herald-retrieval-test
herald-retrieval-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_herald_retrieval.py

review-test: herald-retrieval-test

# Source UID verification and safe per-message moves, against a scripted peer.
.PHONY: imap-mutation-test reply-popup-test
build/imap-mutation-tests: tests/test_imap_mutations.c tests/transfer_tls_double.h src/imap.c $(HOST_SOURCES) $(HOST_HEADERS) | build
	$(HOST_CC) $(HOST_TEST_FLAGS) -Iinclude tests/test_imap_mutations.c $(HOST_SOURCES) -o $@

imap-mutation-test: build/imap-mutation-tests
	./build/imap-mutation-tests

reply-popup-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_reply_popup.py

review-test: imap-mutation-test reply-popup-test

# Native checkbox library ownership; UI state tests live in herald-retrieval-test.
.PHONY: checkbox-lifecycle-test
checkbox-lifecycle-test:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/test_checkbox_lifecycle.py

review-test: checkbox-lifecycle-test
