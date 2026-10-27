# Authentication

SlopTube signs in with a real Google account through the same OAuth **device flow** that
YouTube on TVs uses. No cookies are read or stored, and the password is only ever typed on
Google's own page.

## Flow

1. **Client credentials.** The TV web app (`https://www.youtube.com/tv`) embeds its OAuth client
   id and secret. They are read at sign-in time, so they stay in sync with Google's rotation.
2. **Device code.** `POST https://www.youtube.com/o/oauth2/device/code` with the client id, the
   scope `http://gdata.youtube.com https://www.googleapis.com/auth/youtube`, a random device id
   (UUID v4, generated once) and the device model `ytlr::`. The response holds a user code and
   a verification URL (`youtube.com/activate`).
3. **User approval.** The app shows the code; the user opens the URL (the "Open page" button
   launches the browser) and approves.
4. **Polling.** The app polls `POST https://www.youtube.com/o/oauth2/token` with grant type
   `http://oauth.net/grant_type/device/1.0` at the server-given interval, doubling it on
   `slow_down`, until it receives an access token and a refresh token.
5. **Refresh.** `Session::authorization()` refreshes the access token (grant `refresh_token`)
   two minutes before it expires and returns the `Authorization: Bearer …` header value.

Only the TV InnerTube client accepts this token, which is why metadata requests use TVHTML5 (see
[innertube.md](innertube.md)).

## Multiple accounts

`auth::Session` stores any number of signed-in Google accounts and one active index:

| Method | Effect |
| --- | --- |
| `pollSignIn` (authorized) | Appends the new account and makes it active |
| `setProfile(Account)` | Records name, e-mail, handle and photo of the active account (from `account/accounts_list`); signing in twice to the same e-mail keeps only the newest entry |
| `switchAccount(i)` | Makes another stored account active |
| `signOut()` | Removes the active account; the next one becomes active |
| `accounts()` | Profiles of every stored account, for the switcher |

The app refreshes the profile after start-up, sign-in and switching, and reloads every
account-dependent feed (home, subscriptions, history, playlists) on a switch.

## Storage

Credentials are written to `session.json` in the app's private data directory (the CLI uses
`~/.config/studio.kanye.sloptube/session.json`):

```json
{
  "clientId": "…", "clientSecret": "…", "deviceId": "…", "active": 0,
  "accounts": [
    { "refreshToken": "…", "accessToken": "…", "expiresAt": 1793200000,
      "name": "…", "byline": "…", "handle": "@…", "photo": "https://yt3.ggpht.com/…" }
  ]
}
```

Writes are atomic (temporary file + rename) and the file is created with mode `0600`. Files from
the earlier single-account layout (tokens at the top level) are still read and migrated on the
next save.
