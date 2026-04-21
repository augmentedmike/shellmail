# shellmail

**Email in the terminal. No Electron. No browser. No bullshit.**

A fast, keyboard-driven IMAP email client written in C. Threads, filters, full TLS — all in a ncurses UI that opens instantly and stays out of your way.

![shellmail inbox](docs/inbox-wide.png)

---

## Why

Every email client is either a web app pretending to be native or a native app pretending to be fast. `shellmail` is neither. It's ~3,000 lines of C that connects to your IMAP server, caches everything in SQLite, and renders in your terminal.

No runtime. No VM. No framework. Starts in under a second.

---

## Features

- **Thread view** — conversations grouped and sorted by latest message
- **Background sync** — IMAP fetch runs on a separate thread while you navigate
- **Local cache** — SQLite-backed, survives restarts, loads instantly
- **Hide seen** — `H` filters the list to unread only
- **Vim-style filter rules** — press `:` to define `filter from "x" -> Folder`, moves all matching mail and creates the folder on the server
- **Mark all read** — `M` sends `STORE 1:* +FLAGS (\Seen)` to the server and updates the local cache
- **TLS** — mbedTLS 4.x, no OpenSSL dependency
- **Compose + reply** — SMTP send via TLS

![unread filter](docs/unread.png)

---

## Keys

| Key | Action |
|-----|--------|
| `j` / `k` | Navigate threads |
| `Enter` | Open thread |
| `H` | Toggle hide-seen filter |
| `M` | Mark all as read (server + cache) |
| `:` | Open filter command bar |
| `R` | Force sync |
| `c` | Compose new message |
| `r` | Reply (in reader) |
| `ESC` | Back to list |
| `q` | Quit |

### Filter syntax

```
filter from "newsletter@example.com" -> Newsletters
filter subject "invoice" -> Finance
```

Press `:` with a thread selected — the command bar pre-fills with the sender's address.

---

## Build

**Dependencies** (macOS via Homebrew):

```sh
brew install mbedtls ncurses sqlite
```

**Build:**

```sh
make
```

Output binary: `./shellmail`

---

## Config

Copy `config.yaml.example` to `config.yaml`:

```yaml
imap_server: imap.gmail.com
imap_port: "993"
smtp_server: smtp.gmail.com
smtp_port: "465"
username: you@gmail.com
password: your-app-password
```

For Gmail, use an [App Password](https://myaccount.google.com/apppasswords) — not your account password.

---

## Architecture

```
src/
  imap/       IMAP4rev1 client (TLS, UID FETCH, FLAGS, COPY/EXPUNGE)
  smtp/       SMTP send over TLS
  cache/      SQLite layer (headers, bodies, flags, filters)
  sync/       Background sync thread (pthread + cond var)
  core/       Message types, thread grouping, app state
  ui/         ncurses panes (list, reader, composer, command bar)
  net/        TLS session management (mbedTLS)
```

Everything is single-threaded except the sync worker, which communicates via atomic flags and a mutex/condvar — no shared mutable state between the sync thread and the UI.

---

## Status

Daily driver. Works with Gmail and any standard IMAP server. Written for macOS, should build on Linux with minor Makefile adjustments.

---

Built by [@augmentedmike](https://github.com/augmentedmike)
