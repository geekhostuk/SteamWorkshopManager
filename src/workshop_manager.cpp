#include "workshop_manager.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>

const char* EResultToString(EResult result) {
    switch (result) {
        case k_EResultOK:                     return "OK";
        case k_EResultFail:                   return "Generic failure";
        case k_EResultNoConnection:           return "No network connection";
        case k_EResultInvalidParam:           return "Invalid parameter (item may not exist)";
        case k_EResultFileNotFound:           return "Item not found";
        case k_EResultBusy:                   return "Busy - try again";
        case k_EResultInvalidState:           return "Invalid state";
        case k_EResultAccessDenied:           return "Access denied";
        case k_EResultTimeout:               return "Timed out";
        case k_EResultBanned:                return "Banned";
        case k_EResultServiceUnavailable:     return "Service unavailable";
        case k_EResultNotLoggedOn:            return "Not logged on";
        case k_EResultPending:               return "Pending";
        case k_EResultLimitExceeded:          return "Rate/limit exceeded";
        case k_EResultDuplicateRequest:       return "Duplicate request (already subscribed)";
        case k_EResultIOFailure:             return "IO failure";
        default:                             return "Unknown error";
    }
}

bool WorkshopManager::Initialize() {
    if (!SteamAPI_Init()) {
        std::cerr << "Error: Failed to initialize Steam API.\n"
                  << "Make sure Steam is running and you are logged in.\n"
                  << "Also ensure steam_appid.txt exists in the working directory.\n";
        return false;
    }

    if (!SteamUser()) {
        std::cerr << "Error: ISteamUser interface not available.\n";
        SteamAPI_Shutdown();
        return false;
    }

    if (!SteamUGC()) {
        std::cerr << "Error: ISteamUGC interface not available.\n";
        SteamAPI_Shutdown();
        return false;
    }

    return true;
}

void WorkshopManager::Shutdown() {
    SteamAPI_Shutdown();
}

uint64_t WorkshopManager::GetSteamID64() {
    return SteamUser()->GetSteamID().ConvertToUint64();
}

uint32_t WorkshopManager::GetNumSubscribedItems() {
    return SteamUGC()->GetNumSubscribedItems();
}

std::vector<uint64_t> WorkshopManager::GetSubscribedItems() {
    uint32_t count = SteamUGC()->GetNumSubscribedItems();
    if (count == 0) {
        return {};
    }

    std::vector<PublishedFileId_t> items(count);
    uint32_t actual = SteamUGC()->GetSubscribedItems(items.data(), count);
    items.resize(actual);

    return std::vector<uint64_t>(items.begin(), items.end());
}

// --- Single-item operations ---

bool WorkshopManager::SubscribeItem(uint64_t publishedFileId) {
    auto result = SubscribeBatch({publishedFileId}, 1);
    return result.succeeded == 1;
}

bool WorkshopManager::UnsubscribeItem(uint64_t publishedFileId) {
    auto result = UnsubscribeBatch({publishedFileId}, 1);
    return result.succeeded == 1;
}

// --- Batch internals ---

size_t WorkshopManager::FindSlotByFileId(uint64_t fileId) {
    for (size_t i = 0; i < MAX_BATCH_SIZE; ++i) {
        if (m_slotResults[i].itemId == fileId && !m_slotResults[i].completed) {
            return i;
        }
    }
    return MAX_BATCH_SIZE; // not found
}

void WorkshopManager::OnBatchSubscribeResult(RemoteStorageSubscribePublishedFileResult_t* pResult, bool bIOFailure) {
    size_t slot = FindSlotByFileId(pResult->m_nPublishedFileId);
    if (slot < MAX_BATCH_SIZE) {
        m_slotResults[slot].completed = true;
        m_slotResults[slot].ioFailure = bIOFailure;
        m_slotResults[slot].errorCode = pResult->m_eResult;
        m_slotResults[slot].success = !bIOFailure && pResult->m_eResult == k_EResultOK;
    }
    --m_batchPending;
}

void WorkshopManager::OnBatchUnsubscribeResult(RemoteStorageUnsubscribePublishedFileResult_t* pResult, bool bIOFailure) {
    size_t slot = FindSlotByFileId(pResult->m_nPublishedFileId);
    if (slot < MAX_BATCH_SIZE) {
        m_slotResults[slot].completed = true;
        m_slotResults[slot].ioFailure = bIOFailure;
        m_slotResults[slot].errorCode = pResult->m_eResult;
        m_slotResults[slot].success = !bIOFailure && pResult->m_eResult == k_EResultOK;
    }
    --m_batchPending;
}

void WorkshopManager::OnBatchAddDependencyResult(AddUGCDependencyResult_t* pResult, bool bIOFailure) {
    size_t slot = FindSlotByFileId(pResult->m_nChildPublishedFileID);
    if (slot < MAX_BATCH_SIZE) {
        m_slotResults[slot].completed = true;
        m_slotResults[slot].ioFailure = bIOFailure;
        m_slotResults[slot].errorCode = pResult->m_eResult;
        m_slotResults[slot].success = !bIOFailure && pResult->m_eResult == k_EResultOK;
    }
    --m_batchPending;
}

void WorkshopManager::OnCreateItemResult(CreateItemResult_t* pResult, bool bIOFailure) {
    m_lastCreateItemResult = *pResult;
    if (bIOFailure) {
        m_lastCreateItemResult.m_eResult = k_EResultIOFailure;
    }
    m_singleCallDone = true;
    if (m_batchPending > 0) --m_batchPending;
}

void WorkshopManager::OnSubmitItemUpdateResult(SubmitItemUpdateResult_t* pResult, bool bIOFailure) {
    m_lastSubmitResult = *pResult;
    if (bIOFailure) {
        m_lastSubmitResult.m_eResult = k_EResultIOFailure;
    }
    m_singleCallDone = true;
    if (m_batchPending > 0) --m_batchPending;
}

void WorkshopManager::WaitForBatch() {
    auto start = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::seconds(30);

    while (m_batchPending > 0) {
        SteamAPI_RunCallbacks();
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            std::cerr << "Warning: Batch timed out with " << m_batchPending << " calls still pending.\n";
            // Mark remaining slots as timed out
            for (auto& slot : m_slotResults) {
                if (slot.itemId != 0 && !slot.completed) {
                    slot.completed = true;
                    slot.success = false;
                    slot.errorCode = k_EResultTimeout;
                }
            }
            m_batchPending = 0;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

BatchResult WorkshopManager::SubscribeBatch(const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb) {
    batchSize = std::min(batchSize, MAX_BATCH_SIZE);
    BatchResult total;
    size_t totalProcessed = 0;

    for (size_t offset = 0; offset < items.size(); offset += batchSize) {
        size_t count = std::min(batchSize, items.size() - offset);

        // Reset slots
        for (size_t i = 0; i < count; ++i) {
            m_slotResults[i] = {items[offset + i], k_EResultOK, false, false, false};
        }
        m_batchPending = count;

        for (size_t i = 0; i < count; ++i) {
            SteamAPICall_t hCall = SteamUGC()->SubscribeItem(static_cast<PublishedFileId_t>(items[offset + i]));
            m_subCallResults[i].Set(hCall, this, &WorkshopManager::OnBatchSubscribeResult);
        }

        WaitForBatch();

        // Collect failures for retry
        std::vector<size_t> failedSlots;
        for (size_t i = 0; i < count; ++i) {
            if (m_slotResults[i].success) {
                ++total.succeeded;
            } else {
                failedSlots.push_back(i);
            }
        }

        // Retry failed items
        for (size_t retry = 0; retry < MAX_RETRIES && !failedSlots.empty(); ++retry) {
            std::cerr << "  Retrying " << failedSlots.size() << " failed item(s) (attempt " << (retry + 2) << "/" << (MAX_RETRIES + 1) << ")...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1 + retry)); // back off

            m_batchPending = failedSlots.size();
            for (size_t fi = 0; fi < failedSlots.size(); ++fi) {
                size_t slot = failedSlots[fi];
                m_slotResults[slot].completed = false;
                m_slotResults[slot].success = false;
                SteamAPICall_t hCall = SteamUGC()->SubscribeItem(static_cast<PublishedFileId_t>(m_slotResults[slot].itemId));
                m_subCallResults[slot].Set(hCall, this, &WorkshopManager::OnBatchSubscribeResult);
            }

            WaitForBatch();

            std::vector<size_t> stillFailed;
            for (size_t slot : failedSlots) {
                if (m_slotResults[slot].success) {
                    ++total.succeeded;
                } else {
                    stillFailed.push_back(slot);
                }
            }
            failedSlots = stillFailed;
        }

        // Record permanent failures
        for (size_t slot : failedSlots) {
            ++total.failed;
            total.failedItems.push_back({
                m_slotResults[slot].itemId,
                m_slotResults[slot].errorCode,
                m_slotResults[slot].ioFailure
            });
        }

        totalProcessed += count;
        if (progressCb) {
            progressCb(totalProcessed, items.size(), total.succeeded, total.failed);
        }

        if (offset + count < items.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    return total;
}

BatchResult WorkshopManager::AddDependenciesBatch(uint64_t collectionId, const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb) {
    batchSize = std::min(batchSize, MAX_BATCH_SIZE);
    BatchResult total;
    size_t totalProcessed = 0;

    for (size_t offset = 0; offset < items.size(); offset += batchSize) {
        size_t count = std::min(batchSize, items.size() - offset);

        for (size_t i = 0; i < count; ++i) {
            m_slotResults[i] = {items[offset + i], k_EResultOK, false, false, false};
        }
        m_batchPending = count;

        for (size_t i = 0; i < count; ++i) {
            SteamAPICall_t hCall = SteamUGC()->AddDependency(
                static_cast<PublishedFileId_t>(collectionId),
                static_cast<PublishedFileId_t>(items[offset + i]));
            m_addDepCallResults[i].Set(hCall, this, &WorkshopManager::OnBatchAddDependencyResult);
        }

        WaitForBatch();

        std::vector<size_t> failedSlots;
        for (size_t i = 0; i < count; ++i) {
            if (m_slotResults[i].success) {
                ++total.succeeded;
            } else {
                failedSlots.push_back(i);
            }
        }

        for (size_t retry = 0; retry < MAX_RETRIES && !failedSlots.empty(); ++retry) {
            std::cerr << "  Retrying " << failedSlots.size() << " failed item(s) (attempt " << (retry + 2) << "/" << (MAX_RETRIES + 1) << ")...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1 + retry));

            m_batchPending = failedSlots.size();
            for (size_t fi = 0; fi < failedSlots.size(); ++fi) {
                size_t slot = failedSlots[fi];
                m_slotResults[slot].completed = false;
                m_slotResults[slot].success = false;
                SteamAPICall_t hCall = SteamUGC()->AddDependency(
                    static_cast<PublishedFileId_t>(collectionId),
                    static_cast<PublishedFileId_t>(m_slotResults[slot].itemId));
                m_addDepCallResults[slot].Set(hCall, this, &WorkshopManager::OnBatchAddDependencyResult);
            }

            WaitForBatch();

            std::vector<size_t> stillFailed;
            for (size_t slot : failedSlots) {
                if (m_slotResults[slot].success) {
                    ++total.succeeded;
                } else {
                    stillFailed.push_back(slot);
                }
            }
            failedSlots = stillFailed;
        }

        for (size_t slot : failedSlots) {
            ++total.failed;
            total.failedItems.push_back({
                m_slotResults[slot].itemId,
                m_slotResults[slot].errorCode,
                m_slotResults[slot].ioFailure
            });
        }

        totalProcessed += count;
        if (progressCb) {
            progressCb(totalProcessed, items.size(), total.succeeded, total.failed);
        }

        if (offset + count < items.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    return total;
}

CollectionResult WorkshopManager::CreateCollection(uint32_t appId,
                                                   const std::string& title,
                                                   const std::string& description,
                                                   ERemoteStoragePublishedFileVisibility visibility,
                                                   const std::vector<uint64_t>& items,
                                                   size_t batchSize,
                                                   ProgressCallback progressCb) {
    CollectionResult result;

    // --- Step 1: create the collection UGC item ---
    m_singleCallDone = false;
    m_batchPending = 1;
    m_lastCreateItemResult = {};
    SteamAPICall_t hCreate = SteamUGC()->CreateItem(appId, k_EWorkshopFileTypeCollection);
    m_createItemCallResult.Set(hCreate, this, &WorkshopManager::OnCreateItemResult);
    WaitForBatch();

    if (!m_singleCallDone || m_lastCreateItemResult.m_eResult != k_EResultOK) {
        result.stage = "create";
        result.errorCode = m_singleCallDone ? m_lastCreateItemResult.m_eResult : k_EResultTimeout;
        result.needsLegalAgreement = m_lastCreateItemResult.m_bUserNeedsToAcceptWorkshopLegalAgreement;
        return result;
    }

    result.collectionId = m_lastCreateItemResult.m_nPublishedFileId;
    result.needsLegalAgreement = m_lastCreateItemResult.m_bUserNeedsToAcceptWorkshopLegalAgreement;

    // --- Step 2: set metadata ---
    UGCUpdateHandle_t hUpdate = SteamUGC()->StartItemUpdate(appId, result.collectionId);
    SteamUGC()->SetItemTitle(hUpdate, title.c_str());
    if (!description.empty()) {
        SteamUGC()->SetItemDescription(hUpdate, description.c_str());
    }
    SteamUGC()->SetItemVisibility(hUpdate, visibility);

    // --- Step 3: submit the update to publish the collection ---
    m_singleCallDone = false;
    m_batchPending = 1;
    m_lastSubmitResult = {};
    SteamAPICall_t hSubmit = SteamUGC()->SubmitItemUpdate(hUpdate, "Created via SteamWorkshopManager");
    m_submitUpdateCallResult.Set(hSubmit, this, &WorkshopManager::OnSubmitItemUpdateResult);
    WaitForBatch();

    if (!m_singleCallDone || m_lastSubmitResult.m_eResult != k_EResultOK) {
        result.stage = "submit";
        result.errorCode = m_singleCallDone ? m_lastSubmitResult.m_eResult : k_EResultTimeout;
        if (m_lastSubmitResult.m_bUserNeedsToAcceptWorkshopLegalAgreement) {
            result.needsLegalAgreement = true;
        }
        return result;
    }
    if (m_lastSubmitResult.m_bUserNeedsToAcceptWorkshopLegalAgreement) {
        result.needsLegalAgreement = true;
    }

    result.created = true;

    // --- Step 4: add the workshop items as children of the collection ---
    if (!items.empty()) {
        result.stage = "add-items";
        BatchResult addResult = AddDependenciesBatch(result.collectionId, items, batchSize, progressCb);
        result.itemsAdded = addResult.succeeded;
        result.itemsFailed = addResult.failed;
        result.failedItems = std::move(addResult.failedItems);
    }

    return result;
}

BatchResult WorkshopManager::UnsubscribeBatch(const std::vector<uint64_t>& items, size_t batchSize, ProgressCallback progressCb) {
    batchSize = std::min(batchSize, MAX_BATCH_SIZE);
    BatchResult total;
    size_t totalProcessed = 0;

    for (size_t offset = 0; offset < items.size(); offset += batchSize) {
        size_t count = std::min(batchSize, items.size() - offset);

        for (size_t i = 0; i < count; ++i) {
            m_slotResults[i] = {items[offset + i], k_EResultOK, false, false, false};
        }
        m_batchPending = count;

        for (size_t i = 0; i < count; ++i) {
            SteamAPICall_t hCall = SteamUGC()->UnsubscribeItem(static_cast<PublishedFileId_t>(items[offset + i]));
            m_unsubCallResults[i].Set(hCall, this, &WorkshopManager::OnBatchUnsubscribeResult);
        }

        WaitForBatch();

        std::vector<size_t> failedSlots;
        for (size_t i = 0; i < count; ++i) {
            if (m_slotResults[i].success) {
                ++total.succeeded;
            } else {
                failedSlots.push_back(i);
            }
        }

        for (size_t retry = 0; retry < MAX_RETRIES && !failedSlots.empty(); ++retry) {
            std::cerr << "  Retrying " << failedSlots.size() << " failed item(s) (attempt " << (retry + 2) << "/" << (MAX_RETRIES + 1) << ")...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1 + retry));

            m_batchPending = failedSlots.size();
            for (size_t fi = 0; fi < failedSlots.size(); ++fi) {
                size_t slot = failedSlots[fi];
                m_slotResults[slot].completed = false;
                m_slotResults[slot].success = false;
                SteamAPICall_t hCall = SteamUGC()->UnsubscribeItem(static_cast<PublishedFileId_t>(m_slotResults[slot].itemId));
                m_unsubCallResults[slot].Set(hCall, this, &WorkshopManager::OnBatchUnsubscribeResult);
            }

            WaitForBatch();

            std::vector<size_t> stillFailed;
            for (size_t slot : failedSlots) {
                if (m_slotResults[slot].success) {
                    ++total.succeeded;
                } else {
                    stillFailed.push_back(slot);
                }
            }
            failedSlots = stillFailed;
        }

        for (size_t slot : failedSlots) {
            ++total.failed;
            total.failedItems.push_back({
                m_slotResults[slot].itemId,
                m_slotResults[slot].errorCode,
                m_slotResults[slot].ioFailure
            });
        }

        totalProcessed += count;
        if (progressCb) {
            progressCb(totalProcessed, items.size(), total.succeeded, total.failed);
        }

        if (offset + count < items.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    return total;
}
