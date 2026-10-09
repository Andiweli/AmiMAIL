# ![Logo](https://github.com/Andiweli/AmiMAIL/blob/main/_Images/mail-icon.png) AmiMAIL

**A native Mail client for AmigaOS 3.2+ — built with ReAction, IMAP, SMTP and AmiSSL.**

![Version](https://img.shields.io/badge/version-2.2.2-blue)
![AmigaOS](https://img.shields.io/badge/AmigaOS-3.2%2B-orange)
![Mail](https://img.shields.io/badge/IMAP%20%2F%20SMTP-red)
![ReAction](https://img.shields.io/badge/GUI-ReAction-green)
![AmiSSL](https://img.shields.io/badge/TLS-AmiSSL-lightgrey)
![AI](https://img.shields.io/badge/AI-assisted%20coding-6e7781)
[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

## 🌐 About

AmiMAIL is a lightweight native **Email client for AmigaOS 3.2+**. It provides a classic ReAction interface while connecting directly to any **IMAP and SMTP over AmiSSL/TLS**. Messages remain on the server and are accessed live through IMAP. AmiMAIL does **not** maintain a local offline mail database.

![AmiMAIL App screen](https://github.com/Andiweli/AmiMAIL/blob/main/_Images/amimail1.jpg)
![AmiMAIL App screen with configuration requester](https://github.com/Andiweli/AmiMAIL/blob/main/_Images/amimail2.jpg)

## 📧 Features

- Native **AmigaOS 3.2+ / ReAction** interface
- Support for up to **five independent IMAP/SMTP accounts** with native account tabs in the main window and Account settings
- Per-account server settings, credentials, notification sounds, signatures and encrypted startup/unlock state
- Secure connections using **direct SSL/TLS or STARTTLS** through AmiSSL
- Automatic detection of standard IMAP folders such as **Inbox, Sent, Drafts, Spam/Junk and Trash**, with manual folder assignment when required
- Additional and nested IMAP folders displayed as an expandable folder tree with remembered expand/collapse state
- Independent background Inbox checks for each enabled account, including optional fetch at startup and periodic checks while AmiMAIL is iconified
- Compose new messages, **Reply, Reply All and Forward**, with RFC-compliant sender display names
- Per-account **signature management** with automatic insertion into new messages, replies and forwards
- Move and delete messages, empty Trash and Spam, and mark messages as read/unread or flagged/unflagged
- Multi-selection for supported message operations
- Create, save, reopen, edit and send **IMAP drafts**
- Improved **HTML-to-text conversion** for HTML-only or malformed messages, while preferring clean plain-text alternatives when available
- MIME messages with Base64, Quoted-Printable, RFC 2047 and extended HTML-entity handling
- Send **attachments with a combined maximum of 20 MB**
- Preserve attachments when forwarding messages and save attachments from received messages
- Sort messages by sender, subject, date or message size
- Clickable URLs and **`mailto:` integration with single-instance hand-off**
- Local **Contacts / Address Book** with add, edit and multi-selection delete
- Import contacts from **CSV and VCF/vCard**, including duplicate detection
- Select one or multiple contacts for **To, CC and BCC** while composing mail
- Optional configurable **new-mail notification sound** per account using AmigaOS DataTypes
- Native **ReAction Iconify** support with background mail checks and an embedded Workbench AppIcon
- Built-in asynchronous **GitHub update check and release download to `RAM:`**
- Live mail status through the AmigaOS **ENV/ENVARC** `AmiMAILStatus` variable
- German UI on German AmigaOS systems, English UI otherwise
- **No permanent mailbox database:** messages are read from IMAP; temporary mail and recovery files use a local spool

## 🔐 Account Security

AmiMAIL encrypts stored credentials with **AES-256-GCM** and creates a random
256-bit key for each account. It stores that key beside the encrypted
configuration in `ENVARC:AmiMail`, so accounts can log in automatically. There
is no master-password prompt in the normal account settings.

This protects the configuration from casual reading and detects modified
encrypted data. It does **not** protect credentials from someone who can read
both the `.cfg` and matching `.key` files, including from a disk image or backup.
Protect the account directory and its backups. PBKDF2-HMAC-SHA256 remains
supported for older master-password configurations; normal saves use random keys.

Fetched messages and outgoing recovery copies can contain unencrypted mail
and attachments in `PROGDIR:Spool` (or `AmiMAIL_SpoolDir`). Interrupted operations
may retain recovery files to prevent data loss. Protect this drawer and inspect
retained files before deleting them while AmiMAIL is stopped.

## 🌐 Installation

1. Download the latest AmiMAIL release and extract it to an Amiga drawer.
2. Install **AmiSSL 5.x** and make sure the `AmiSSL:` assign and CA certificates are available.
3. Make sure the Amiga has a working TCP/IP connection and the correct system date and time.
4. Start `AmiMAIL`.
5. Open the account settings and configure your server.
6. Save the account settings; AmiMAIL creates its local encryption key automatically.

## ⚙️ Requirements

- **AmigaOS 3.2+**
- 68020 CPU or newer
- ReAction
- TCP/IP stack with `bsdsocket.library` V4
- **AmiSSL 5.x** with a valid `AmiSSL:` assign and current CA certificates
- Correct system date and time for TLS certificate validation

AmiMAIL has been tested on accelerated Amiga systems including **PiStorm32 / CM4**, from HiRes Interlace configurations up to higher-resolution P96 screens.

## 🌍 Languages

AmiMAIL supports:

- **German** when AmigaOS is configured for German
- **English** on all other system language configurations

Additional interface languages are not planned.

## ⚠️ Limitations

- No local/offline mail cache
- No local full-text search database
- Plain-text message composition rather than an HTML editor
- German and English user interface only

## 🔗 References

- [AmiSSL project](https://github.com/jens-maus/amissl)

## ❤️ Support

If you enjoy this project and would like to support my work, you can make a small contribution via PayPal.

Your support helps me spend more time maintaining existing projects, fixing bugs, improving compatibility, and working on new features.

[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

Thank you for your support!

## 📧 Legal

AmiMAIL is licensed under the **AmiMAIL/AmiGmail Non-Commercial Source License 1.0**. Free use, modification and non-commercial redistribution are permitted. Commercial distribution requires prior written permission from the copyright holder. See `LICENSE` for the complete terms.

AmiMAIL is an independent, non-commercial hobby project. **Amiga** and **AmigaOS** are trademarks of their respective owners. AmiSSL and other third-party components remain subject to their respective licenses.

Copyright © Andreas 'Andiweli' Stürmer.

## Building and checking changes

`make release` and `make debug` use separate native object directories and copy
the selected executable to `bin/AmiMAIL`. Run `make review-test host-check` on a
host with GCC and Python. Native GUI/network tests use API doubles; they do not
replace an AmigaOS runtime test.

`make dist` and `make release-lha` stage the same binary package, including the
icon, catalogs, guides, sounds and Installer source files. For the optional
Installer, enter the extracted `Install` drawer and run
`SYS:Utilities/Installer Install-AmiMAIL`. Manual installation by copying the
extracted program drawer remains available. The shipped icon is used; the old
Mason/Classic chooser is retained when both separate icons are supplied.

`make source-dist` archives tracked working-tree files only. It excludes Git
metadata, build outputs, backup sources and the private OAuth configuration
header. Commit new source files before creating the source archive.
