#pragma once

#include "steam_api.h"
#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <functional>

static constexpr size_t MAX_BATCH_SIZE = 50;
static constexpr size_t MAX_RETRIES = 3;

struct FailedItem {
    uint64_t id;
    EResult errorCode;
    bool ioFailure;
};

struct BatchResult {
    size_t succeeded = 0;
    size_t failed = 0;
    std::vector<FailedItem> failedItems;
};

// Convert EResult to a human-readable string
const char* EResultToString(EResult result);

class WorkshopManager {
public:
    bool Initialize();
    void Shutdown();

    uint64_t GetSteamID64();
    std::vector<uint64_t> GetSubscribedItems();
    uint32_t GetNumSubscribedItems();

    // Single-item async operations (blocks until complete, up to 30s timeout)
    bool SubscribeItem(uint64_t publishedFileId);
    bool UnsubscribeItem(uint64_t publishedFileId);

    // Batch operations — fires batchSize concurrent calls, waits for all, repeats.
    // Automatically retries failed items up to MAX_RETRIES times.
    // Calls progressCb(completed, total, succeeded, failed) after each batch.
    using ProgressCallback = std::function<void(size_t completed, size_t total, size_t succeeded, size_t failed)>;
    BatchResult SubscribeBatch(const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb = nullptr);
    BatchResult UnsubscribeBatch(const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb = nullptr);

private:
    void OnBatchSubscribeResult(RemoteStorageSubscribePublishedFileResult_t* pResult, bool bIOFailure);
    void OnBatchUnsubscribeResult(RemoteStorageUnsubscribePublishedFileResult_t* pResult, bool bIOFailure);

    void WaitForBatch();

    // Per-slot tracking for the current batch
    struct SlotResult {
        uint64_t itemId = 0;
        EResult errorCode = k_EResultOK;
        bool ioFailure = false;
        bool completed = false;
        bool success = false;
    };

    size_t m_batchPending = 0;
    std::array<SlotResult, MAX_BATCH_SIZE> m_slotResults;

    std::array<CCallResult<WorkshopManager, RemoteStorageSubscribePublishedFileResult_t>, MAX_BATCH_SIZE> m_subCallResults;
    std::array<CCallResult<WorkshopManager, RemoteStorageUnsubscribePublishedFileResult_t>, MAX_BATCH_SIZE> m_unsubCallResults;

    // Map from callback PublishedFileId back to slot index
    size_t FindSlotByFileId(uint64_t fileId);
};
