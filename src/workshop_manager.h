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

struct CollectionResult {
    bool created = false;
    uint64_t collectionId = 0;
    bool needsLegalAgreement = false;
    EResult errorCode = k_EResultOK;
    std::string stage;        // "create" | "submit" | "add-items"
    size_t itemsAdded = 0;
    size_t itemsFailed = 0;
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

    // Creates a Workshop collection on the logged-in account (scoped to appId) and
    // populates it with the given workshop item IDs. Blocks until complete.
    CollectionResult CreateCollection(uint32_t appId,
                                      const std::string& title,
                                      const std::string& description,
                                      ERemoteStoragePublishedFileVisibility visibility,
                                      const std::vector<uint64_t>& items,
                                      size_t batchSize,
                                      ProgressCallback progressCb = nullptr);

private:
    void OnBatchSubscribeResult(RemoteStorageSubscribePublishedFileResult_t* pResult, bool bIOFailure);
    void OnBatchUnsubscribeResult(RemoteStorageUnsubscribePublishedFileResult_t* pResult, bool bIOFailure);
    void OnBatchAddDependencyResult(AddUGCDependencyResult_t* pResult, bool bIOFailure);
    void OnCreateItemResult(CreateItemResult_t* pResult, bool bIOFailure);
    void OnSubmitItemUpdateResult(SubmitItemUpdateResult_t* pResult, bool bIOFailure);

    // Adds each item as a dependency (child) of the collection in batches, with retries.
    BatchResult AddDependenciesBatch(uint64_t collectionId, const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb = nullptr);

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
    std::array<CCallResult<WorkshopManager, AddUGCDependencyResult_t>, MAX_BATCH_SIZE> m_addDepCallResults;

    // Single async-call tracking for CreateItem / SubmitItemUpdate
    CCallResult<WorkshopManager, CreateItemResult_t> m_createItemCallResult;
    CCallResult<WorkshopManager, SubmitItemUpdateResult_t> m_submitUpdateCallResult;
    CreateItemResult_t m_lastCreateItemResult = {};
    SubmitItemUpdateResult_t m_lastSubmitResult = {};
    bool m_singleCallDone = false;

    // Map from callback PublishedFileId back to slot index
    size_t FindSlotByFileId(uint64_t fileId);
};
