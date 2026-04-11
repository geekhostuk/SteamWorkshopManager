#include "workshop_manager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
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
    bool appIdOverridden = false;
    size_t batchSize = 10;
};

static void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --backup               Export subscriptions to <SteamID>.json\n"
              << "  --list                 Print all subscribed Workshop items\n"
              << "  --unsubscribe-all      Unsubscribe from all Workshop items\n"
              << "  --restore <file.json>  Subscribe to all items in a backup file\n"
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
        } else if (arg == "--appid") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --appid requires a numeric argument.\n";
                return false;
            }
            try {
                opts.appId = static_cast<uint32_t>(std::stoul(argv[++i]));
                opts.appIdOverridden = true;
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
    if (!opts.backup && !opts.list && !opts.unsubscribeAll && opts.restoreFile.empty()) {
        std::cerr << "Error: No action specified.\n\n";
        PrintUsage(argv[0]);
        return false;
    }

    return true;
}

static void WriteSteamAppIdFile(uint32_t appId) {
    std::ofstream f("steam_appid.txt");
    if (f.is_open()) {
        f << appId << "\n";
    }
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

static int DoRestore(WorkshopManager& mgr, const std::string& filename, bool dryRun, size_t batchSize) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: Could not open " << filename << "\n";
        return 1;
    }

    json j;
    try {
        f >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse JSON: " << e.what() << "\n";
        return 1;
    }

    if (!j.contains("subscriptions") || !j["subscriptions"].is_array()) {
        std::cerr << "Error: JSON file missing 'subscriptions' array.\n";
        return 1;
    }

    auto subscriptions = j["subscriptions"].get<std::vector<uint64_t>>();
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

int main(int argc, char* argv[]) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        return 1;
    }

    // Write steam_appid.txt if AppID was overridden
    if (opts.appIdOverridden) {
        WriteSteamAppIdFile(opts.appId);
    }

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

    mgr.Shutdown();
    return result;
}
