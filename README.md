# Steam Workshop Subscription Manager

CLI tool for managing Steam Workshop subscriptions across multiple accounts. Back up subscribed item IDs to JSON, bulk unsubscribe, and restore/copy subscriptions between accounts. Built with the Steamworks SDK (ISteamUGC) and nlohmann/json. Supports dry-run mode and custom AppIDs.

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
- **Dry Run** — Preview any destructive operation without making changes
- **Multi-Account** — Backup files are per-account, enabling easy subscription migration

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Operating System** | Windows 10/11 (64-bit). Linux is supported but untested. |
| **Steam Client** | Must be installed, running, and logged into the desired account |
| **Steamworks SDK** | Version 1.64 (see [download instructions](#downloading-the-steamworks-sdk) below) |
| **CMake** | 3.20 or newer |
| **C++ Compiler** | MSVC (Visual Studio 2022 Build Tools recommended), or GCC/Clang on Linux |
| **Internet Connection** | Required during the first build to fetch the nlohmann/json dependency |

---

## Downloading the Steamworks SDK

The Steamworks SDK is **not** distributed with this project. You must download it yourself:

1. Go to [https://partner.steamgames.com/](https://partner.steamgames.com/) and log in with your Steam account
2. Navigate to **Documentation > Steamworks SDK** and download the SDK (version 1.64 or compatible)
3. Extract the archive so the directory structure looks like:
   ```
   steamworks_sdk_164/
     sdk/
       public/
         steam/
           steam_api.h
           isteamugc.h
           ...
       redistributable_bin/
         win64/
           steam_api64.dll
           steam_api64.lib
   ```
4. Note the full path to the `sdk` folder — you will need it for the build step

The default path expected by the build is `C:/Projects/Tools/steamworks_sdk_164/sdk`. If your SDK is elsewhere, you can override this (see [Building from Source](#building-from-source)).

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

cmake -B build -DSTEAMWORKS_SDK_DIR="/path/to/steamworks_sdk_164/sdk"
cmake --build build --config Release
```

### What the Build Does Automatically

- Fetches [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 via CMake FetchContent (first build only)
- Copies `steam_api64.dll` (Windows) or `libsteam_api.so` (Linux) to the build output directory
- Creates `steam_appid.txt` containing `410340` (the default AppID) in the build output directory

### Build Output

After building, the output directory (`build/Release/` on Windows) will contain:

```
build/Release/
  steam-subber.exe        # The tool
  steam_api64.dll         # Steam runtime library (copied from SDK)
  steam_appid.txt         # Default AppID file (410340)
```

---

## Initial Setup

1. **Make sure Steam is running** and logged into the account you want to manage
2. **Navigate to the build output directory**:
   ```bash
   cd build/Release
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
  --appid <id>           Override AppID (default: 410340)
  --dry-run              Simulate without making changes
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

Found 3 subscribed items.
Are you sure you want to unsubscribe from ALL 3 items? [y/N] y
[1/3] Unsubscribing from 123456789... OK
[2/3] Unsubscribing from 234567890... OK
[3/3] Unsubscribing from 345678901... OK

Done. Unsubscribed: 3, Failed: 0
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
Items to restore: 3
[1/3] Subscribing to 123456789... OK
[2/3] Subscribing to 234567890... OK
[3/3] Subscribing to 345678901... OK

Done. Subscribed: 3, Failed: 0
```

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

## Troubleshooting

### "Failed to initialize Steam API"

- **Steam is not running**: Start the Steam client and log in before running the tool
- **Wrong working directory**: Run the tool from the directory containing `steam_api64.dll` and `steam_appid.txt` (usually `build/Release/`)
- **Missing steam_appid.txt**: The build should create this automatically. If missing, create it manually with the content `410340` (or your desired AppID)

### "ISteamUGC interface not available"

- The Steam client may be outdated. Update Steam and try again.

### Callback timeout (operation reported as FAILED)

- Steam servers may be temporarily unavailable. Wait a moment and retry.
- Check your internet connection.

### No items appear in --list but you have subscriptions

- Make sure the AppID matches the game whose subscriptions you want to manage. Use `--appid <id>` to specify the correct game.
- The default AppID is `410340` (Liftoff). If you're managing a different game, you must specify its AppID.

### Build errors about missing headers

- Verify the `STEAMWORKS_SDK_DIR` path points to the `sdk` folder (not the parent archive folder)
- The path should contain `public/steam/steam_api.h`

---

## License

This project is provided as-is for personal use. The Steamworks SDK is property of Valve Corporation and subject to the [Steamworks SDK license agreement](https://partner.steamgames.com/doc/sdk/license).
