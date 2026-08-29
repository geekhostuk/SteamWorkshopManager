#include "workshop_manager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <chrono>

using json = nlohmann::json;

struct Options {
    bool backup = false;
    bool list = false;
    bool unsubscribeAll = false;
    bool dryRun = false;
    std::string restoreFile;
    uint32_t appId = 410340;
    size_t batchSize = 10;

    // Create-collection options
    bool createCollection = false;
    std::string collectionTitle;
    std::string collectionDesc;
    ERemoteStoragePublishedFileVisibility collectionVisibility = k_ERemoteStoragePublishedFileVisibilityPrivate;
    bool collectionFromSubscribed = false;
    std::string collectionFromFile;
};

static void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --backup               Export subscriptions to <SteamID>.json\n"
              << "  --list                 Print all subscribed Workshop items\n"
              << "  --unsubscribe-all      Unsubscribe from all Workshop items\n"
              << "  --restore <file.json>  Subscribe to all items in a backup file\n"
              << "  --create-collection <title>\n"
              << "                         Create a Workshop collection and add items to it\n"
              << "  --collection-desc <text>\n"
              << "                         Description for the new collection (optional)\n"
              << "  --collection-visibility <public|friends|private|unlisted>\n"
              << "                         Visibility of the new collection (default: private)\n"
              << "  --from-subscribed      Populate collection from current subscriptions\n"
              << "  --from-file <file.json>\n"
              << "                         Populate collection from a backup file's items\n"
              << "  --appid <id>           Override AppID (default: 410340)\n"
              << "  --dry-run              Simulate without making changes\n"
              << "  --batch-size <n>       Concurrent operations per batch (default: 10, max: 50)\n"
              << "  --help                 Show this help message\n";
}

static std::string GetISO8601Timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

static bool ParseArgs(int argc, char* argv[], Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--backup") {
            opts.backup = true;
        } else if (arg == "--list") {
            opts.list = true;
        } else if (arg == "--unsubscribe-all") {
            opts.unsubscribeAll = true;
        } else if (arg == "--dry-run") {
            opts.dryRun = true;
        } else if (arg == "--restore") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --restore requires a filename argument.\n";
                return false;
            }
            opts.restoreFile = argv[++i];
        } else if (arg == "--create-collection") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --create-collection requires a title argument.\n";
                return false;
            }
            opts.createCollection = true;
            opts.collectionTitle = argv[++i];
        } else if (arg == "--collection-desc") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --collection-desc requires a text argument.\n";
                return false;
            }
            opts.collectionDesc = argv[++i];
        } else if (arg == "--collection-visibility") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --collection-visibility requires a value.\n";
                return false;
            }
            std::string vis = argv[++i];
            if (vis == "public") {
                opts.collectionVisibility = k_ERemoteStoragePublishedFileVisibilityPublic;
            } else if (vis == "friends") {
                opts.collectionVisibility = k_ERemoteStoragePublishedFileVisibilityFriendsOnly;
            } else if (vis == "private") {
                opts.collectionVisibility = k_ERemoteStoragePublishedFileVisibilityPrivate;
            } else if (vis == "unlisted") {
                opts.collectionVisibility = k_ERemoteStoragePublishedFileVisibilityUnlisted;
            } else {
                std::cerr << "Error: Invalid --collection-visibility '" << vis << "'. "
                          << "Use public, friends, private, or unlisted.\n";
                return false;
            }
        } else if (arg == "--from-subscribed") {
            opts.collectionFromSubscribed = true;
        } else if (arg == "--from-file") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --from-file requires a filename argument.\n";
                return false;
            }
            opts.collectionFromFile = argv[++i];
        } else if (arg == "--appid") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --appid requires a numeric argument.\n";
                return false;
            }
            try {
                opts.appId = static_cast<uint32_t>(std::stoul(argv[++i]));
            } catch (...) {
                std::cerr << "Error: Invalid AppID value.\n";
                return false;
            }
        } else if (arg == "--batch-size") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --batch-size requires a numeric argument.\n";
                return false;
            }
            try {
                opts.batchSize = static_cast<size_t>(std::stoul(argv[++i]));
                if (opts.batchSize == 0) opts.batchSize = 1;
                if (opts.batchSize > MAX_BATCH_SIZE) opts.batchSize = MAX_BATCH_SIZE;
            } catch (...) {
                std::cerr << "Error: Invalid batch size value.\n";
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'.\n";
            PrintUsage(argv[0]);
            return false;
        }
    }

    // Must specify at least one action
    if (!opts.backup && !opts.list && !opts.unsubscribeAll && opts.restoreFile.empty() && !opts.createCollection) {
        std::cerr << "Error: No action specified.\n\n";
        PrintUsage(argv[0]);
        return false;
    }

    // Validate create-collection inputs
    if (opts.createCollection) {
        if (opts.collectionTitle.empty()) {
            std::cerr << "Error: --create-collection requires a non-empty title.\n";
            return false;
        }
        bool hasFileSource = !opts.collectionFromFile.empty();
        if (opts.collectionFromSubscribed == hasFileSource) {
            // Both set, or neither set
            std::cerr << "Error: --create-collection requires exactly one item source: "
                      << "--from-subscribed or --from-file <file.json>.\n";
            return false;
        }
    }

    return true;
}

// Tell the Steam API which AppID to initialize as.
//
// This deliberately sets the SteamAppId environment variable rather than
// writing steam_appid.txt. The environment variable is process-local, so it
// leaves nothing behind in the working directory, and it takes precedence over
// an existing steam_appid.txt — which also repairs directories left holding a
// stale AppID by older versions of this tool.
static void SetSteamAppId(uint32_t appId) {
    const std::string value = std::to_string(appId);
#ifdef _WIN32
    _putenv_s("SteamAppId", value.c_str());
#else
    setenv("SteamAppId", value.c_str(), 1);
#endif
}

static int DoList(WorkshopManager& mgr) {
    auto items = mgr.GetSubscribedItems();
    if (items.empty()) {
        std::cout << "No subscribed Workshop items found.\n";
        return 0;
    }

    std::cout << "Subscribed Workshop items (" << items.size() << "):\n";
    for (auto id : items) {
        std::cout << "  " << id << "\n";
    }
    return 0;
}

static int DoBackup(WorkshopManager& mgr, uint32_t appId) {
    uint64_t steamId = mgr.GetSteamID64();
    auto items = mgr.GetSubscribedItems();

    json j;
    j["steamid"] = std::to_string(steamId);
    j["appid"] = appId;
    j["timestamp"] = GetISO8601Timestamp();
    j["subscriptions"] = items;

    std::string filename = std::to_string(steamId) + ".json";
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: Could not open " << filename << " for writing.\n";
        return 1;
    }

    f << j.dump(2) << "\n";
    f.close();

    std::cout << "Backed up " << items.size() << " subscriptions to " << filename << "\n";
    return 0;
}

static int DoUnsubscribeAll(WorkshopManager& mgr, bool dryRun, size_t batchSize) {
    auto items = mgr.GetSubscribedItems();
    if (items.empty()) {
        std::cout << "No subscribed Workshop items to unsubscribe from.\n";
        return 0;
    }

    std::cout << "Found " << items.size() << " subscribed items.\n";

    if (dryRun) {
        std::cout << "[DRY RUN] Would unsubscribe from " << items.size() << " items:\n";
        for (auto id : items) {
            std::cout << "  " << id << "\n";
        }
        return 0;
    }

    std::cout << "Are you sure you want to unsubscribe from ALL " << items.size() << " items? [y/N] ";
    std::string response;
    std::getline(std::cin, response);
    if (response != "y" && response != "Y") {
        std::cout << "Aborted.\n";
        return 0;
    }

    std::cout << "Unsubscribing (batch size: " << batchSize << ")...\n";
    auto result = mgr.UnsubscribeBatch(items, batchSize, [](size_t completed, size_t total, size_t succeeded, size_t failed) {
        std::cout << "  [" << completed << "/" << total << "] OK: " << succeeded << ", Failed: " << failed << "\n";
        std::cout.flush();
    });

    std::cout << "\nDone. Unsubscribed: " << result.succeeded << ", Failed: " << result.failed << "\n";

    if (!result.failedItems.empty()) {
        std::cout << "\nFailed items:\n";
        for (const auto& f : result.failedItems) {
            std::cout << "  " << f.id << " — ";
            if (f.ioFailure) {
                std::cout << "IO failure (network error)\n";
            } else {
                std::cout << EResultToString(f.errorCode) << " (code " << static_cast<int>(f.errorCode) << ")\n";
            }
        }
    }

    return (result.failed > 0) ? 1 : 0;
}

// Reads the "subscriptions" array from a backup JSON file. Prints an error and
// returns false on failure. When outJson is non-null it receives the parsed document.
static bool ReadSubscriptionsFile(const std::string& filename, std::vector<uint64_t>& outItems, json* outJson = nullptr) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: Could not open " << filename << "\n";
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse JSON: " << e.what() << "\n";
        return false;
    }

    if (!j.contains("subscriptions") || !j["subscriptions"].is_array()) {
        std::cerr << "Error: JSON file missing 'subscriptions' array.\n";
        return false;
    }

    outItems = j["subscriptions"].get<std::vector<uint64_t>>();
    if (outJson) *outJson = std::move(j);
    return true;
}

static int DoRestore(WorkshopManager& mgr, const std::string& filename, bool dryRun, size_t batchSize) {
    json j;
    std::vector<uint64_t> subscriptions;
    if (!ReadSubscriptionsFile(filename, subscriptions, &j)) {
        return 1;
    }

    if (subscriptions.empty()) {
        std::cout << "No subscriptions in backup file.\n";
        return 0;
    }

    // Show backup metadata
    if (j.contains("steamid")) {
        std::cout << "Backup from SteamID: " << j["steamid"].get<std::string>() << "\n";
    }
    if (j.contains("appid")) {
        std::cout << "Backup AppID: " << j["appid"].get<uint32_t>() << "\n";
    }
    if (j.contains("timestamp")) {
        std::cout << "Backup timestamp: " << j["timestamp"].get<std::string>() << "\n";
    }

    std::cout << "Items to restore: " << subscriptions.size() << "\n";

    if (dryRun) {
        std::cout << "[DRY RUN] Would subscribe to " << subscriptions.size() << " items:\n";
        for (auto id : subscriptions) {
            std::cout << "  " << id << "\n";
        }
        return 0;
    }

    std::cout << "Subscribing (batch size: " << batchSize << ")...\n";
    auto result = mgr.SubscribeBatch(subscriptions, batchSize, [](size_t completed, size_t total, size_t succeeded, size_t failed) {
        std::cout << "  [" << completed << "/" << total << "] OK: " << succeeded << ", Failed: " << failed << "\n";
        std::cout.flush();
    });

    std::cout << "\nDone. Subscribed: " << result.succeeded << ", Failed: " << result.failed << "\n";

    if (!result.failedItems.empty()) {
        std::cout << "\nFailed items:\n";
        for (const auto& f : result.failedItems) {
            std::cout << "  " << f.id << " — ";
            if (f.ioFailure) {
                std::cout << "IO failure (network error)\n";
            } else {
                std::cout << EResultToString(f.errorCode) << " (code " << static_cast<int>(f.errorCode) << ")\n";
            }
        }
    }

    return (result.failed > 0) ? 1 : 0;
}

static const char* VisibilityToString(ERemoteStoragePublishedFileVisibility v) {
    switch (v) {
        case k_ERemoteStoragePublishedFileVisibilityPublic:      return "public";
        case k_ERemoteStoragePublishedFileVisibilityFriendsOnly: return "friends";
        case k_ERemoteStoragePublishedFileVisibilityPrivate:     return "private";
        case k_ERemoteStoragePublishedFileVisibilityUnlisted:    return "unlisted";
        default:                                                 return "unknown";
    }
}

static int DoCreateCollection(WorkshopManager& mgr, const Options& opts) {
    // Resolve the item IDs from the chosen source.
    std::vector<uint64_t> items;
    if (opts.collectionFromSubscribed) {
        items = mgr.GetSubscribedItems();
    } else {
        if (!ReadSubscriptionsFile(opts.collectionFromFile, items)) {
            return 1;
        }
    }

    std::cout << "Collection title:   " << opts.collectionTitle << "\n";
    if (!opts.collectionDesc.empty()) {
        std::cout << "Description:        " << opts.collectionDesc << "\n";
    }
    std::cout << "Visibility:         " << VisibilityToString(opts.collectionVisibility) << "\n";
    std::cout << "Items to include:   " << items.size() << "\n";

    if (items.empty()) {
        std::cout << "Warning: no items found for this source; an empty collection will be created.\n";
    }

    if (opts.dryRun) {
        std::cout << "[DRY RUN] Would create the collection with these items:\n";
        for (auto id : items) {
            std::cout << "  " << id << "\n";
        }
        return 0;
    }

    std::cout << "Creating collection (batch size: " << opts.batchSize << ")...\n";
    auto result = mgr.CreateCollection(opts.appId, opts.collectionTitle, opts.collectionDesc,
                                       opts.collectionVisibility, items, opts.batchSize,
                                       [](size_t completed, size_t total, size_t succeeded, size_t failed) {
        std::cout << "  [" << completed << "/" << total << "] Added: " << succeeded << ", Failed: " << failed << "\n";
        std::cout.flush();
    });

    if (!result.created) {
        std::cerr << "\nError: Failed to create collection during '" << result.stage << "' stage — "
                  << EResultToString(result.errorCode) << " (code " << static_cast<int>(result.errorCode) << ")\n";
        if (result.needsLegalAgreement) {
            std::cerr << "You must first accept the Steam Workshop Legal Agreement:\n"
                      << "  https://steamcommunity.com/sharedfiles/workshoplegalagreement\n";
        }
        return 1;
    }

    std::cout << "\nCollection created (id " << result.collectionId << ").\n"
              << "  https://steamcommunity.com/sharedfiles/filedetails/?id=" << result.collectionId << "\n";
    std::cout << "Items added: " << result.itemsAdded << ", Failed: " << result.itemsFailed << "\n";

    if (!result.failedItems.empty()) {
        std::cout << "\nFailed items:\n";
        for (const auto& f : result.failedItems) {
            std::cout << "  " << f.id << " — ";
            if (f.ioFailure) {
                std::cout << "IO failure (network error)\n";
            } else {
                std::cout << EResultToString(f.errorCode) << " (code " << static_cast<int>(f.errorCode) << ")\n";
            }
        }
    }

    if (result.needsLegalAgreement) {
        std::cout << "\nNote: the collection stays hidden until you accept the Steam Workshop Legal Agreement:\n"
                  << "  https://steamcommunity.com/sharedfiles/workshoplegalagreement\n";
    }

    return (result.itemsFailed > 0) ? 1 : 0;
}

int main(int argc, char* argv[]) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        return 1;
    }

    // Always set the AppID for this process — both the default and any
    // --appid override — so the tool runs from any working directory and
    // never mutates steam_appid.txt.
    SetSteamAppId(opts.appId);

    WorkshopManager mgr;
    if (!mgr.Initialize()) {
        return 1;
    }

    std::cout << "Logged in as SteamID: " << mgr.GetSteamID64() << "\n\n";

    int result = 0;

    if (opts.list) {
        result = DoList(mgr);
    }

    if (result == 0 && opts.backup) {
        result = DoBackup(mgr, opts.appId);
    }

    if (result == 0 && opts.unsubscribeAll) {
        result = DoUnsubscribeAll(mgr, opts.dryRun, opts.batchSize);
    }

    if (result == 0 && !opts.restoreFile.empty()) {
        result = DoRestore(mgr, opts.restoreFile, opts.dryRun, opts.batchSize);
    }

    if (result == 0 && opts.createCollection) {
        result = DoCreateCollection(mgr, opts);
    }

    mgr.Shutdown();
    return result;
}
