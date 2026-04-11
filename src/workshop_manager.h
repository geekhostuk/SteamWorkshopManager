#pragma once

#include "steam_api.h"
#include <vector>
#include <cstdint>

class WorkshopManager {
public:
    bool Initialize();
    void Shutdown();

    uint64_t GetSteamID64();
    std::vector<uint64_t> GetSubscribedItems();
    uint32_t GetNumSubscribedItems();

    // Async operations — blocks internally until Steam responds (up to 30s timeout)
    bool SubscribeItem(uint64_t publishedFileId);
    bool UnsubscribeItem(uint64_t publishedFileId);

private:
    void OnSubscribeResult(RemoteStorageSubscribePublishedFileResult_t* pResult, bool bIOFailure);
    void OnUnsubscribeResult(RemoteStorageUnsubscribePublishedFileResult_t* pResult, bool bIOFailure);

    void WaitForCallback();

    bool m_bCallCompleted = false;
    bool m_bCallSuccess = false;

    CCallResult<WorkshopManager, RemoteStorageSubscribePublishedFileResult_t> m_subscribeCallResult;
    CCallResult<WorkshopManager, RemoteStorageUnsubscribePublishedFileResult_t> m_unsubscribeCallResult;
};
