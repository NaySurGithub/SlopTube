# Command-line tool

`sloptube-cli` drives the core library from a desktop shell. It is the fastest way to check a
parser change or an endpoint against the live service.

```sh
cmake --preset host-debug && cmake --build build/host-debug
build/host-debug/tools/cli/sloptube-cli <command> [args]
```

| Command | Output |
| --- | --- |
| `login` / `logout` | Device-flow sign-in (prints the code and URL) / forget the active account |
| `home`, `subs`, `history`, `playlists`, `shorts` `[pages]` | Feed items, following continuations as the app does (`appendPage`) |
| `search <query> [filters]` | Search results (with each channel's avatar URL). Filters: `popular`, `hour`, `today`, `week`, `month`, `year`, `video`, `short`, `channel`, `playlist`, `movie`, `under3`, `medium`, `long`, `live`, `hd`, `4k`, `subtitles` |
| `suggest <partial query>` | Search suggestions |
| `playlist <id> [pages]` | Playlist entries |
| `watch <video id>` | Title, views, date, channel (subscribed?), likes, rating, comment count, description, chapters, related videos |
| `comments <video id>` | First comments page and the replies of the first thread that has some |
| `channel <channel id>` | Name, handle, subscribers, avatar, banner and every shelf |
| `tab <channel id> videos\|shorts\|live\|playlists [pages]` | One channel tab (MWEB), following continuations; nothing when the channel has no such tab |
| `chat <live video id> [polls]` | Live chat messages, polled as often as the server asks |
| `avatar <channel or video id>` | Avatar URL (anonymous MWEB lookups; a video id is resolved to its uploader first) |
| `accounts` | Profiles visible to the signed-in account |
| `posts [channel id]` | Posts of subscribed channels as the home feed shows them, or one channel's posts tab |
| `post <channel id> [index]` | One post of that channel opened like its page: post, comments, replies of the first thread with some |
| `player <video id>` | Playable formats (itag, resolution, bitrate, MIME type, segment length for a live stream) and storyboard levels |

The session lives in `~/.config/studio.kanye.sloptube/session.json` (mode `0600`). The CLI never
likes, subscribes or comments, so it is safe to run against a personal account.
