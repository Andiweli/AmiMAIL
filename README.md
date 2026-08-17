# ![Logo](https://github.com/Andiweli/AmiMAIL/blob/main/images/amimail-icon.png) AmiMAIL

**A native Mail client for AmigaOS 3.2+ — built with ReAction, IMAP, SMTP and AmiSSL.**

![Version](https://img.shields.io/badge/version-1.0RC2-blue)
![AmigaOS](https://img.shields.io/badge/AmigaOS-3.2%2B-orange)
![Mail](https://img.shields.io/badge/IMAP%20%2F%20SMTP-red)
![ReAction](https://img.shields.io/badge/GUI-ReAction-green)
![AmiSSL](https://img.shields.io/badge/TLS-AmiSSL-lightgrey)
![AI](https://img.shields.io/badge/AI-assisted%20coding-6e7781)
[![PayPal](https://img.shields.io/badge/PayPal-Support%20this%20project-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

## 🌐 About

AmiMAIL is a lightweight native **Email client for AmigaOS 3.2+**. It provides a classic ReAction interface while connecting directly to any **IMAP and SMTP over AmiSSL/TLS**. Messages remain on the server and are accessed live through IMAP. AmiMAIL does **not** maintain a local offline mail database.

![AmiMAIL App screen with configuration requester](https://github.com/Andiweli/AmiMAIL/blob/main/images/amimail-app.png)

## 📧 Features

- Native **AmigaOS 3.2+ / ReAction** interface
- **Single-account IMAP/SMTP client** with freely configurable server names, ports and usernames
- Secure connections using **direct SSL/TLS or STARTTLS** through AmiSSL
- Automatic detection of standard IMAP folders such as **Inbox, Sent, Drafts, Spam/Junk and Trash**, with manual folder assignment when required
- Additional and nested IMAP folders displayed as an expandable folder tree with remembered expand/collapse state
- Optional automatic Inbox fetch when AmiMAIL starts
- Optional automatic Inbox check every 5 minutes, including while AmiMAIL is iconified
- Compose new messages and reply to received mail
- Move and delete messages, empty Trash and Spam, and mark messages as read/unread or flagged/unflagged
- Multi-selection for supported message operations
- Create, save, reopen, edit and send **IMAP drafts**
- MIME messages with Base64, Quoted-Printable and RFC 2047 handling
- Send up to **8 attachments with a combined maximum of 10 MB**
- Save attachments from received messages
- Sort messages by sender, subject, date or message size
- Clickable URLs and **`mailto:` integration with single-instance hand-off**
- Local **Contacts / Address Book** with add, edit and multi-selection delete
- Import contacts from **CSV and VCF/vCard**, including duplicate detection
- Select one or multiple contacts for **To, CC and BCC** while composing mail
- Optional configurable **new-mail notification sound** using AmigaOS DataTypes
- Native **ReAction Iconify** support with background mail checks and an embedded Workbench AppIcon
- Window position and size are restored between program starts
- Built-in asynchronous **GitHub update check and release download to `RAM:`**
- Live mail status through the AmigaOS **ENV/ENVARC** `AmiMAILStatus` variable
- German UI on German AmigaOS systems, English UI otherwise
- **No local email cache:** messages are read directly from the IMAP server

## 🔐 Account Security

AmiMAIL stores the account data encrypted using a **mandatory user-defined master password**.

Sensitive account information is protected with:

- **PBKDF2-HMAC-SHA256** key derivation
- **AES-256-GCM** authenticated encryption
- encrypted storage of IMAP/SMTP passwords or app passwords

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

[![PayPal](https://img.shields.io/badge/PayPal-Support%20this%20project-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

AmiMAIL is an independent, non-commercial hobby project. **Amiga** and **AmigaOS** are trademarks of their respective owners. AmiSSL and other third-party components remain subject to their respective licenses.

Copyright © Andreas 'Andiweli' Stürmer.
