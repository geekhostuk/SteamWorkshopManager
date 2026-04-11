cmake_minimum_required(VERSION 3.20)
project(steam-subber LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Steamworks SDK path — override with -DSTEAMWORKS_SDK_DIR=...
set(STEAMWORKS_SDK_DIR "C:/Projects/Tools/steamworks_sdk_164/sdk" CACHE PATH "Path to the Steamworks SDK")

# nlohmann/json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Executable
add_executable(steam-subber src/main.cpp src/workshop_manager.cpp)

# Steamworks includes (headers use flat includes like #include "steam_api.h")
target_include_directories(steam-subber PRIVATE "${STEAMWORKS_SDK_DIR}/public/steam")

# Platform-specific linking
if(WIN32)
    target_link_libraries(steam-subber PRIVATE
        "${STEAMWORKS_SDK_DIR}/redistributable_bin/win64/steam_api64.lib"
        nlohmann_json::nlohmann_json
    )

    # Copy steam_api64.dll to output directory
    add_custom_command(TARGET steam-subber POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STEAMWORKS_SDK_DIR}/redistributable_bin/win64/steam_api64.dll"
            "$<TARGET_FILE_DIR:steam-subber>"
        COMMENT "Copying steam_api64.dll to output directory"
    )
else()
    # Linux
    target_link_libraries(steam-subber PRIVATE
        "${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64/libsteam_api.so"
        nlohmann_json::nlohmann_json
    )

    # Copy libsteam_api.so to output directory
    add_custom_command(TARGET steam-subber POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64/libsteam_api.so"
            "$<TARGET_FILE_DIR:steam-subber>"
        COMMENT "Copying libsteam_api.so to output directory"
    )
endif()

# Create steam_appid.txt in output directory
add_custom_command(TARGET steam-subber POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "410340" > "$<TARGET_FILE_DIR:steam-subber>/steam_appid.txt"
    COMMENT "Creating steam_appid.txt with default AppID 410340"
)
