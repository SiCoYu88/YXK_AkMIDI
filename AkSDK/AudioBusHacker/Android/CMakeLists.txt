cmake_minimum_required(VERSION 3.22)
project(AudioBusHackerAndroid LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK toolchain and set ANDROID_ABI.")
endif()
set(WWISE_SDK "$ENV{WWISESDK}" CACHE PATH "Matching Wwise SDK directory")
set(ABH_CONFIGURATION "Profile" CACHE STRING "Debug, Profile or Release")
set_property(CACHE ABH_CONFIGURATION PROPERTY STRINGS Debug Profile Release)
if(NOT ABH_CONFIGURATION MATCHES "^(Debug|Profile|Release)$")
    message(FATAL_ERROR "Unknown ABH_CONFIGURATION: ${ABH_CONFIGURATION}")
endif()
if(NOT EXISTS "${WWISE_SDK}/include/AK/AkWwiseSDKVersion.h")
    message(FATAL_ERROR "WWISE_SDK must contain include/AK/AkWwiseSDKVersion.h")
endif()

set(plugin_sources "${CMAKE_CURRENT_LIST_DIR}/../SoundEnginePlugin")
add_library(AudioBusHackerFX STATIC
    "${plugin_sources}/AudioBusHackerFX.cpp"
    "${plugin_sources}/AudioBusHackerFXParams.cpp")
target_include_directories(AudioBusHackerFX PUBLIC "${WWISE_SDK}/include" "${plugin_sources}")
target_compile_features(AudioBusHackerFX PUBLIC cxx_std_14)
set_target_properties(AudioBusHackerFX PROPERTIES
    POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_compile_options(AudioBusHackerFX PRIVATE -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
if(ABH_CONFIGURATION STREQUAL "Debug")
    target_compile_definitions(AudioBusHackerFX PUBLIC _DEBUG AK_ENABLE_ASSERTS)
elseif(ABH_CONFIGURATION STREQUAL "Profile")
    target_compile_definitions(AudioBusHackerFX PUBLIC NDEBUG AK_ENABLE_ASSERTS)
else()
    target_compile_definitions(AudioBusHackerFX PUBLIC NDEBUG AK_OPTIMIZED)
endif()

# The UE integration uses the static library. The shared library is for Wwise's
# dynamic plugin loader; do not load both copies in the same game process.
add_library(AudioBusHacker SHARED "${plugin_sources}/AudioBusHackerFXShared.cpp")
target_link_libraries(AudioBusHacker PRIVATE AudioBusHackerFX log m)
target_link_options(AudioBusHacker PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)

set(sdk_destination "SDK/Android_${ANDROID_ABI}/${ABH_CONFIGURATION}")
install(TARGETS AudioBusHackerFX ARCHIVE DESTINATION "${sdk_destination}/lib")
install(TARGETS AudioBusHacker LIBRARY DESTINATION "${sdk_destination}/bin")
install(FILES "${plugin_sources}/AudioBusHackerFXFactory.h" DESTINATION SDK/include/AK/Plugin)

option(ABH_BUILD_TESTS "Link a static plugin consumer and test callback/DSP on Android" OFF)
if(ABH_BUILD_TESTS)
    add_executable(AudioBusHackerAndroidTest "${CMAKE_CURRENT_LIST_DIR}/../Tests/AndroidSmoke.cpp")
    target_link_libraries(AudioBusHackerAndroidTest PRIVATE AudioBusHackerFX log m)
    target_link_options(AudioBusHackerAndroidTest PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
endif()
