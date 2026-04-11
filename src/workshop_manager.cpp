#include "workshop_manager.h"
#include <chrono>
#include <thread>
#include <iostream>

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

    // PublishedFileId_t is uint64, same as uint64_t
    return std::vector<uint64_t>(items.begin(), items.end());
}

bool WorkshopManager::SubscribeItem(uint64_t publishedFileId) {
    m_bCallCompleted = false;
    m_bCallSuccess = false;

    SteamAPICall_t hCall = SteamUGC()->SubscribeItem(static_cast<PublishedFileId_t>(publishedFileId));
    m_subscribeCallResult.Set(hCall, this, &WorkshopManager::OnSubscribeResult);

    WaitForCallback();
    return m_bCallSuccess;
}

bool WorkshopManager::UnsubscribeItem(uint64_t publishedFileId) {
    m_bCallCompleted = false;
    m_bCallSuccess = false;

    SteamAPICall_t hCall = SteamUGC()->UnsubscribeItem(static_cast<PublishedFileId_t>(publishedFileId));
    m_unsubscribeCallResult.Set(hCall, this, &WorkshopManager::OnUnsubscribeResult);

    WaitForCallback();
    return m_bCallSuccess;
}

void WorkshopManager::OnSubscribeResult(RemoteStorageSubscribePublishedFileResult_t* pResult, bool bIOFailure) {
    m_bCallSuccess = !bIOFailure && pResult->m_eResult == k_EResultOK;
    m_bCallCompleted = true;
}

void WorkshopManager::OnUnsubscribeResult(RemoteStorageUnsubscribePublishedFileResult_t* pResult, bool bIOFailure) {
    m_bCallSuccess = !bIOFailure && pResult->m_eResult == k_EResultOK;
    m_bCallCompleted = true;
}

void WorkshopManager::WaitForCallback() {
    auto start = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::seconds(30);

    while (!m_bCallCompleted) {
        SteamAPI_RunCallbacks();
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            std::cerr << "Warning: Callback timed out after 30 seconds.\n";
            m_bCallCompleted = true;
            m_bCallSuccess = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
