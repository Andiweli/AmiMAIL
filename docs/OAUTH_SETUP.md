# Optional Google OAuth support in AmiMail

AmiMail's normal account GUI uses password/app-password authentication for
IMAP and SMTP. The retained Google OAuth 2.0 + PKCE code is optional and kept
provider-specific so the generic mail core does not depend on Google.

## Existing low-level support

The source can:

- generate PKCE verifier/challenge/state values
- build the Google authorization URL
- exchange authorization/refresh data against Google's token endpoint
- build XOAUTH2 authentication payloads for IMAP/SMTP

The Google mail scope used by the optional module is:

```text
https://mail.google.com/
```

## What is not exposed in the current GUI

A complete interactive OAuth sign-in workflow (browser launch, loopback
callback and account-settings integration) is not currently exposed by the
AmiMail account dialog. This is intentional: OAuth is not required for the
single-account generic IMAP/SMTP release target.

If OAuth is expanded later, provider endpoints/scopes should remain outside the
generic IMAP/SMTP account layer.
