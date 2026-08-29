# Steam Workshop Subscription Manager

CLI tool for managing Steam Workshop subscriptions across multiple accounts. Back up subscribed item IDs to JSON, bulk unsubscribe, restore/copy subscriptions between accounts, and create Workshop collections from your subscriptions or a backup file. Built with the Steamworks SDK (ISteamUGC) and nlohmann/json. Supports dry-run mode and custom AppIDs.

---

## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Downloading the Steamworks SDK](#downloading-the-steamworks-sdk)
- [Building from Source](#building-from-source)
- [Initial Setup](#initial-setup)
- [Usage](#usage)
- [Multi-Account Workflow](#multi-account-workflow)
- [Backup File Format](#backup-file-format)
- [Troubleshooting](#troubleshooting)

---

## Features

- **Backup** — Export all subscribed Workshop item IDs to a JSON file, named by SteamID64
- **Restore** — Subscribe to every item in a backup file (copy subscriptions between accounts)
- **Unsubscribe All** — Bulk unsubscribe from every Workshop item, with a safety confirmation prompt
- **List** — Print all currently subscribed Workshop items
- **Create Collection** — Create a Workshop collection and populate it from your current subscriptions or a backup file
- **Dry Run** — Preview any destructive operation without making changes
- **Batch Processing** — Concurrent operations (configurable batch size) for fast bulk subscribe/unsubscribe
- **Auto-Retry** — Failed items are automatically retried up to 3 times with backoff, with detailed error reporting
- **Multi-Account** — Backup files are per-account, enabling easy subscription migration

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Operating System** | Windows 10/11 (64-bit), or Linux (64-bit). Both are tested. |
| **Steam Client** | Must be installed, running, and logged into the desired account |
| **Steamworks SDK** | Version 1.64 or 1.65 (see [download instructions](#downloading-the-steamworks-sdk) below) |
| **CMake** | 3.20 or newer |
| **C++ Compiler** | MSVC (Visual Studio 2022 Build Tools recommended), or GCC/Clang on Linux |
| **Internet Connection** | Required during the first build to fetch the nlohmann/json dependency |

Linux additionally needs the Steam client's `steamclient.so` bridge at
`~/.steam/sdk64` (the Steam client normally creates this symlink itself), and a
**native** Steam install rather than a Flatpak one — see
[Troubleshooting](#troubleshooting).

The Linux build is verified on Arch Linux (kernel 7.1) with GCC 16.2, CMake 4.4
and Steamworks SDK 1.65, against a native Steam client.

---

## Downloading the Steamworks SDK

The Steamworks SDK is **not** distributed with this project. You must download it yourself:

1. Go to [https://partner.steamgames.com/](https://partner.steamgames.com/) and log in with your Steam account
2. Navigate to **Documentation > Steamworks SDK** and download the SDK (version 1.64 or 1.65)
3. Extract the archive so the directory structure looks like:
   ```
   steamworks_sdk_165/
     sdk/
       public/
         steam/
           steam_api.h
           isteamugc.h
           ...
       redistributable_bin/
         win64/
           steam_api64.dll        # Windows
           steam_api64.lib
         linux64/
           libsteam_api.so        # Linux
   ```
4. Note the full path to the `sdk` folder — you will need it for the build step

The default path expected by the build is `C:/Projects/Tools/steamworks_sdk_164/sdk`. This
is a Windows path, so on Linux you **must** override it (see
[Building from Source](#building-from-source)).

### A note on SDK versions

The SDK must match the interface versions exported by your installed Steam
client. Both 1.64 and 1.65 currently work. If you hit a compile error such as

```
error: 'struct AddUGCDependencyResult_t' has no member named 'm_nChildPublishedFileID';
       did you mean 'm_nChildPublishedFileId'?
```

you are on an SDK whose field casing differs from the one the source was
written against — correct the casing to match your SDK's `isteamugc.h`.

---

## Building from Source

### Windows (MSVC)

```bash
# Clone the repo
git clone https://github.com/geekhostuk/SteamWorkshopManager.git
cd SteamWorkshopManager

# Configure (uses default SDK path)
cmake -B build

# Or specify a custom SDK location
cmake -B build -DSTEAMWORKS_SDK_DIR="D:/path/to/steamworks_sdk_164/sdk"

# Build
cmake --build build --config Release
```

### Linux (GCC/Clang)

```bash
git clone https://github.com/geekhostuk/SteamWorkshopManager.git
cd SteamWorkshopManager

cmake -B build -DSTEAMWORKS_SDK_DIR="/path/to/steamworks_sdk_165/sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Note:** the default CMake generator on Linux is single-config, so the build
type must be set at *configure* time with `-DCMAKE_BUILD_TYPE=Release`.
Passing `--config Release` to `cmake --build` is silently ignored and you will
get an unoptimized build. The output lands in `build/`, not `build/Release/`.

Linux also requires the Steam client's `steamclient.so` bridge at
`~/.steam/sdk64`. The Steam client normally creates this symlink itself; if
`SteamAPI_Init` fails, verify it exists:

```bash
ls -l ~/.steam/sdk64        # -> ~/.local/share/Steam/linux64
```

### What the Build Does Automatically

- Fetches [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 via CMake FetchContent (first build only)
- Copies `steam_api64.dll` (Windows) or `libsteam_api.so` (Linux) to the build output directory
- Creates `steam_appid.txt` containing `410340` (the default AppID) in the build output
  directory. This is a fallback only: the tool sets the `SteamAppId` environment variable
  for its own process on every run, so it does not depend on this file and never writes to it.

### Build Output

After building, the output directory (`build/Release/` on Windows, `build/` on Linux) will contain:

Windows:

```
build/Release/
  steam-subber.exe        # The tool
  steam_api64.dll         # Steam runtime library (copied from SDK)
  steam_appid.txt         # Default AppID file (410340)
```

Linux:

```
build/
  steam-subber            # The tool
  libsteam_api.so         # Steam runtime library (copied from SDK)
  steam_appid.txt         # Default AppID file (410340)
```

On Linux the executable is linked with an `$ORIGIN` rpath, so it finds the
copied `libsteam_api.so` sitting beside it. The whole output directory can be
moved elsewhere and will still run, even if the SDK is deleted.

---

## Initial Setup

1. **Make sure Steam is running** and logged into the account you want to manage
2. **Navigate to the build output directory**:
   ```bash
   cd build/Release      # Windows
   cd build              # Linux
   ```
3. **Verify the tool can connect to Steam**:
   ```bash
   ./steam-subber --list
   ```
   You should see your SteamID printed and a list of subscribed items (or "No subscribed Workshop items found" if you have none).

If you get an error about failing to initialize the Steam API, see [Troubleshooting](#troubleshooting).

---

## Usage

```
steam-subber [options]

Options:
  --backup               Export subscriptions to <SteamID>.json
  --list                 Print all subscribed Workshop items
  --unsubscribe-all      Unsubscribe from all Workshop items (with confirmation)
  --restore <file.json>  Subscribe to all items in a backup file
  --create-collection <title>
                         Create a Workshop collection and add items to it
  --collection-desc <text>
                         Description for the new collection (optional)
  --collection-visibility <public|friends|private|unlisted>
                         Visibility of the new collection (default: private)
  --from-subscribed      Populate collection from current subscriptions
  --from-file <file.json>
                         Populate collection from a backup file's items
  --appid <id>           Override AppID (default: 410340)
  --dry-run              Simulate without making changes
  --batch-size <n>       Concurrent operations per batch (default: 10, max: 50)
  --help                 Show help
```

### List Subscriptions

Print all Workshop items the current account is subscribed to:

```bash
./steam-subber --list
```

Output:
```
Logged in as SteamID: 76561198012345678

Subscribed Workshop items (3):
  123456789
  234567890
  345678901
```

### Backup Subscriptions

Export all subscribed item IDs to a JSON file named after your SteamID64:

```bash
./steam-subber --backup
```

Output:
```
Logged in as SteamID: 76561198012345678

Backed up 3 subscriptions to 76561198012345678.json
```

The backup file is created in the current working directory.

### Unsubscribe from Everything

Remove all Workshop subscriptions from the current account:

```bash
./steam-subber --unsubscribe-all
```

Output:
```
Logged in as SteamID: 76561198012345678

Found 200 subscribed items.
Are you sure you want to unsubscribe from ALL 200 items? [y/N] y
Unsubscribing (batch size: 10)...
  [10/200] OK: 10, Failed: 0
  [20/200] OK: 20, Failed: 0
  ...
  [200/200] OK: 200, Failed: 0

Done. Unsubscribed: 200, Failed: 0
```

Preview first with `--dry-run`:

```bash
./steam-subber --dry-run --unsubscribe-all
```

### Restore Subscriptions from a Backup

Subscribe the current account to every item in a backup file:

```bash
./steam-subber --restore 76561198012345678.json
```

Output:
```
Logged in as SteamID: 76561198099999999

Backup from SteamID: 76561198012345678
Backup AppID: 410340
Backup timestamp: 2026-04-11T10:00:00Z
Items to restore: 500
Subscribing (batch size: 10)...
  [10/500] OK: 10, Failed: 0
  [20/500] OK: 20, Failed: 0
  ...
  [500/500] OK: 497, Failed: 3

Done. Subscribed: 497, Failed: 3

Failed items:
  123456789 — Item not found (code 9)
  234567890 — Access denied (code 15)
  345678901 — Item not found (code 9)
```

Failed items are automatically retried up to 3 times before being reported. Common failure reasons:

| Error | Meaning |
|---|---|
| Item not found (9) | Workshop item was deleted by the author |
| Access denied (15) | Item is private or region-locked |
| Rate/limit exceeded (25) | Steam throttled requests (retries handle this) |
| IO failure | Transient network error (retries handle this) |

### Create a Collection

Create a Workshop collection on the logged-in account and populate it with items.
Choose exactly one item source: `--from-subscribed` (your current subscriptions)
or `--from-file <backup.json>` (the items in a backup file).

```bash
# Build a collection from everything you're currently subscribed to
./steam-subber --create-collection "My Favorite Mods" --from-subscribed

# Build a collection from a backup file, with a description, visible to friends
./steam-subber --create-collection "Server Pack" \
  --collection-desc "Mods for our private server" \
  --collection-visibility friends \
  --from-file 76561198012345678.json
```

Output:
```
Logged in as SteamID: 76561198012345678

Collection title:   My Favorite Mods
Visibility:         private
Items to include:   3
Creating collection (batch size: 10)...
  [3/3] Added: 3, Failed: 0

Collection created (id 305676543).
  https://steamcommunity.com/sharedfiles/filedetails/?id=305676543
Items added: 3, Failed: 0
```

Notes:

- The new collection defaults to **private** visibility. Use `--collection-visibility`
  with `public`, `friends`, `private`, or `unlisted` to change it.
- The collection is created for the active `--appid`, and its items should belong to
  the same game.
- The first time an account creates Workshop content, Steam may require you to accept
  the [Workshop Legal Agreement](https://steamcommunity.com/sharedfiles/workshoplegalagreement)
  before the collection becomes visible. The tool prints a reminder when this applies.
- Preview without making changes using `--dry-run`.

### Using a Different Game

The default AppID is `410340` (Liftoff). To manage subscriptions for a different game:

```bash
# Backup subscriptions for Team Fortress 2 (AppID 440)
./steam-subber --appid 440 --backup

# Restore for a different game
./steam-subber --appid 440 --restore backup.json
```

The `--appid` flag overrides `steam_appid.txt` at runtime.

---

## Multi-Account Workflow

The tool operates in the context of whichever Steam account is currently logged into the Steam client. The Steamworks SDK connects to the running Steam client process, so **switching accounts requires signing out and back in within Steam itself**.

### Copying Subscriptions from Account A to Account B

```
Step 1:  Log into Steam as Account A
Step 2:  Run:  steam-subber --backup
         Creates: 76561198AAAAAAAA.json

Step 3:  In Steam, sign out of Account A and sign in as Account B

Step 4:  (Optional) Clear Account B's existing subscriptions:
         Run:  steam-subber --unsubscribe-all

Step 5:  Run:  steam-subber --restore 76561198AAAAAAAA.json
         Account B is now subscribed to all of Account A's items
```

### Syncing Two Accounts

To give both accounts identical subscriptions:

1. Back up both accounts first (`--backup` while logged into each)
2. Restore Account A's backup onto Account B
3. Restore Account B's backup onto Account A

Backup files are named by SteamID64, so multiple backups coexist in the same directory without conflicts.

---

## Backup File Format

```json
{
  "steamid": "76561198012345678",
  "appid": 410340,
  "timestamp": "2026-04-11T10:00:00Z",
  "subscriptions": [
    123456789,
    987654321
  ]
}
```

| Field | Type | Description |
|---|---|---|
| `steamid` | string | SteamID64 of the account that was backed up |
| `appid` | number | The AppID used when the backup was created |
| `timestamp` | string | ISO 8601 UTC timestamp of when the backup was taken |
| `subscriptions` | number[] | Array of Workshop item PublishedFileIds |

---

## Performance

The tool processes subscribe/unsubscribe operations in concurrent batches. The `--batch-size` flag controls how many operations run simultaneously (default: 10).

| Batch Size | ~Time for 1,000 items |
|---|---|
| 1 | ~3.5 min |
| 10 (default) | ~25 sec |
| 25 | ~12 sec |
| 50 (max) | ~8 sec |

```bash
# Default batch size (10)
./steam-subber --restore backup.json

# Faster for large imports
./steam-subber --batch-size 25 --restore backup.json
```

Start with the default. If you see no failures, you can increase the batch size for subsequent runs.

---

## Troubleshooting

### "Failed to initialize Steam API"

- **Steam is not running**: Start the Steam client and log in before running the tool
- **Wrong working directory**: On Windows, run the tool from the directory containing
  `steam_api64.dll` (`build/Release/`). On Linux the executable carries an `$ORIGIN`
  rpath and finds its own `libsteam_api.so`, so it runs from anywhere. The AppID is
  set per-process, so `steam_appid.txt` does not need to be in the working directory.
- **Missing steam_appid.txt**: The build should create this automatically. If missing, create it manually with the content `410340` (or your desired AppID)
- **AppID you cannot initialize**: `--appid <id>` fails here if Steam cannot start as
  that app (for example a game you do not own). Only that run is affected — the AppID
  is set per-process and nothing is written to disk, so later runs are unaffected.
- **Linux — missing `~/.steam/sdk64`**: `libsteam_api.so` loads the Steam client
  through this path. It should point at your Steam install's `linux64` directory:
  ```bash
  ls -l ~/.steam/sdk64      # -> ~/.local/share/Steam/linux64 (contains steamclient.so)
  ```
  Starting the Steam client once normally recreates it.
- **Linux — Flatpak Steam**: a sandboxed Steam client cannot be reached over the
  `steam.pipe` IPC socket from a host-side binary. Use a native Steam package, or
  run the tool inside the Flatpak sandbox.

### Linux: "error while loading shared libraries: libsteam_api.so"

The executable cannot find the Steam runtime library. Confirm `libsteam_api.so`
sits next to the binary and that the rpath includes `$ORIGIN`:

```bash
readelf -d ./steam-subber | grep RUNPATH
```

As a fallback, run with `LD_LIBRARY_PATH=. ./steam-subber --list`.

### "ISteamUGC interface not available"

- The Steam client may be outdated. Update Steam and try again.

### Items reported as FAILED

- The tool automatically retries failed items up to 3 times with increasing backoff
- After all retries, a detailed failure summary is printed with the item ID and error reason
- **Item not found (code 9)**: The Workshop item was deleted by its author — nothing you can do
- **Access denied (code 15)**: The item is private or restricted
- **Rate/limit exceeded (code 25)**: Try again with a smaller `--batch-size`
- **Timeout (code 16)**: Steam servers may be slow — try again later

### No items appear in --list but you have subscriptions

- Make sure the AppID matches the game whose subscriptions you want to manage. Use `--appid <id>` to specify the correct game.
- The default AppID is `410340` (Liftoff). If you're managing a different game, you must specify its AppID.

### Build errors about missing headers

- Verify the `STEAMWORKS_SDK_DIR` path points to the `sdk` folder (not the parent archive folder)
- The path should contain `public/steam/steam_api.h`
- On Linux, the path must also contain `redistributable_bin/linux64/libsteam_api.so`
- For errors about missing *struct members* rather than missing headers, see
  [A note on SDK versions](#a-note-on-sdk-versions)

---

## License

This project is provided as-is for personal use. The Steamworks SDK is property of Valve Corporation and subject to the [Steamworks SDK license agreement](https://partner.steamgames.com/doc/sdk/license).
