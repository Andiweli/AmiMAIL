# ![Logo](https://github.com/Andiweli/AmiMAIL/blob/main/images/amimail-icon.png) AmiMAIL

**A native Mail client for AmigaOS 3.2+ — built with ReAction, IMAP, SMTP and AmiSSL.**

![Version](https://img.shields.io/badge/version-1.0RC-blue)
![AmigaOS](https://img.shields.io/badge/AmigaOS-3.2%2B-orange)
![Mail](https://img.shields.io/badge/Gmail-IMAP%20%2F%20SMTP-red)
![ReAction](https://img.shields.io/badge/GUI-ReAction-green)
![AmiSSL](https://img.shields.io/badge/TLS-AmiSSL-lightgrey)
![AI](https://img.shields.io/badge/AI-assisted%20coding-6e7781)

## 🌐 About

AmiMAIL is a lightweight native **Email client for AmigaOS 3.2+**. It provides a classic ReAction interface while connecting directly to any **IMAP and SMTP over AmiSSL/TLS**. Messages remain on the server and are accessed live through IMAP. AmiMAIL does **not** maintain a local offline mail database.

## 📧 Features

- Native **AmigaOS 3.2+ / ReAction** interface
- Single Gmail account with Gmail's **Inbox, Sent, All Mail, Trash, Spam and Drafts**
- Gmail labels displayed as an expandable folder tree with [+] and [-] icons
- Nested labels can be expanded and collapsed; the folder view state is remembered
- Optional automatic Inbox fetch when AmiGmail starts
- Optional automatic Inbox check every 5 minutes
- Compose new mail and reply to messages
- Move messages between Gmail folders/labels and back to the Inbox
- Delete Spam and empty Gmail Trash
- Mark messages as read or unread
- Multi-selection for supported mail operations
- Open, edit and save Gmail drafts
- MIME messages and file attachments
- Send attachments **up to 8 files and 10 MB**
- Save attachments from received messages
- Sort messages by sender, subject, date or message size
- Clickable URLs via OpenURL or IBrowse `mailto:%h`
- MIME, Base64, Quoted-Printable and RFC 2047 handling
- Version 1.4 introduced an update notification
- Version 1.4 saves the window position and size upon closing
- Version 1.5 allows AmiGmail to be iconified
- Version 1.5 allows notifications to play when new email(s) arrive
- Version 1.6 introduced an Address book/Contacts with CSV/VCF import
- Secure IMAP/SMTP connections through **AmiSSL**
- German UI on German AmigaOS systems, English fallback otherwise
- **No local email cache:** messages are read directly from Gmail via IMAP

## 🔐 Account Security

AmiMAIL stores the account data encrypted using a **mandatory user-defined master password**.

Sensitive account information is protected with:

- **PBKDF2-HMAC-SHA256** key derivation
- **AES-256-GCM** authenticated encryption
- encrypted storage of the Gmail App Password

For Gmail authentication, an **App Password** is used instead of the normal Google account password. This requires two-step verification to be enabled for the Google account.

## 🌐 Installation

1. Download the latest AmiMAIL release and extract it to an Amiga drawer.
2. Install **AmiSSL 5.x** and make sure the `AmiSSL:` assign and CA certificates are available.
3. Make sure the Amiga has a working TCP/IP connection and the correct system date and time.
4. Start `AmiMAIL`.
5. Open the account settings and configure your server.
6. Enter a master password to encrypt the stored account data.

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

- Single account only
- No local/offline mail cache
- No local full-text search database
- Plain-text message composition rather than an HTML editor
- German and English user interface only

## 🔗 References

- [AmiSSL project](https://github.com/jens-maus/amissl)

## 📧 Legal

AmiMAIL is an independent, non-commercial hobby project. **Amiga** and **AmigaOS** are trademarks of their respective owners. AmiSSL and other third-party components remain subject to their respective licenses.

Copyright © Andreas 'Andiweli' Stürmer.
