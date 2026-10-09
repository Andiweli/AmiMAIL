# ![Logo](https://github.com/Andiweli/AmiMAIL/blob/main/_Images/mail-icon.png) AmiMAIL

**A native Mail client for AmigaOS 3.2+ — built with ReAction, IMAP, SMTP and AmiSSL.**

![Version](https://img.shields.io/badge/version-2.3.0-blue)
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

A native AmigaOS 3.2+ mail client with a ReAction interface.

### Accounts and connections

- Manage multiple **IMAP/SMTP accounts** with individual server settings, credentials and signatures.
- Add, remove, reorder and enable or disable accounts.
- Connect using **SSL/TLS or STARTTLS** through AmiSSL.
- Configure independent startup and periodic Inbox checks for each account.
- Log in automatically using locally encrypted account credentials.

### Messages and folders

- Compose messages, **Reply, Reply All and Forward**.
- Create, save, reopen, edit and send **IMAP drafts**.
- Detect standard system folders automatically or assign them manually.
- Browse nested folders in an expandable tree with remembered state.
- Move and delete messages, including multiple selected messages.
- Empty Trash and Spam/Junk folders; mark messages as read/unread or flagged/unflagged.
- Sort messages by **date** and display their reply status.
- Read plain-text messages and text converted from HTML mail, with MIME and character-encoding support.

### Attachments and contacts

- Send attachments and preserve them when forwarding messages.
- Save selected attachments and embedded images from received mail.
- Remember the last attachment save directory.
- Manage a local **Contacts / Address Book**.
- Import **CSV and VCF/vCard** files with duplicate detection.
- Select contacts for **To, CC and BCC**.

### Notifications and interface

- Configure per-account notification sounds and optional **Herald notifications** with new-mail counts and subjects.
- Keep checking for mail while AmiMAIL is iconified.
- View transfer progress and cancel supported operations.
- Restore window positions, sizes and the message-list/preview layout.
- Use keyboard shortcuts, mouse-wheel scrolling and native message selection.

### Desktop integration

- Open clickable web links and email addresses from the mail preview.
- Handle **mailto:** links through the existing AmiMAIL instance.
- Check for updates and download releases from GitHub to **RAM:**.
- Expose mail status through the **AmiMAILStatus** environment variable.
- Use the **English or German interface**, with documentation in both languages.

### Mail storage

- Access messages directly through IMAP without a permanent offline mailbox database.
- Process temporary mail and attachments on disk, retaining recovery copies after failed or uncertain transfers.

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
