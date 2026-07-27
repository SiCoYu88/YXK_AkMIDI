LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := AudioBusHacker
LOCAL_MODULE_FILENAME := libAudioBusHacker

LOCAL_SRC_FILES := AudioBusHackerFXShared.cpp

ifeq ($(PM5_CONFIG),debug_android_armeabi-v7a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -D_DEBUG -mfloat-abi=softfp -mfpu=neon -fno-omit-frame-pointer -g
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),debug_android_x86)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -D_DEBUG -fno-omit-frame-pointer -g
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),debug_android_arm64-v8a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -D_DEBUG -fno-omit-frame-pointer -g
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),debug_android_x86_64)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -D_DEBUG -fno-omit-frame-pointer -g
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_android_armeabi-v7a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -mfloat-abi=softfp -mfpu=neon -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_android_x86)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_android_arm64-v8a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_android_x86_64)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_enableasserts_android_armeabi-v7a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_ENABLE_ASSERTS -mfloat-abi=softfp -mfpu=neon -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_enableasserts_android_x86)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_ENABLE_ASSERTS -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_enableasserts_android_arm64-v8a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_ENABLE_ASSERTS -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),profile_enableasserts_android_x86_64)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_ENABLE_ASSERTS -ffunction-sections -fdata-sections -fno-omit-frame-pointer -g -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  cmd-strip :=
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),release_android_armeabi-v7a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_OPTIMIZED -mfloat-abi=softfp -mfpu=neon -ffunction-sections -fdata-sections -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),release_android_x86)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_OPTIMIZED -ffunction-sections -fdata-sections -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),release_android_arm64-v8a)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_OPTIMIZED -ffunction-sections -fdata-sections -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(PM5_CONFIG),release_android_x86_64)
  LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../SDK/include
  LOCAL_STATIC_LIBRARIES := AudioBusHackerFX
  LOCAL_LDFLAGS := -Wl,--as-needed -Wl,--export-dynamic
  LOCAL_CFLAGS := -DNDEBUG -DAK_OPTIMIZED -ffunction-sections -fdata-sections -O3
  LOCAL_CPPFLAGS := -Wno-conversion-null -Wno-deprecated-declarations -fvisibility=hidden -fno-rtti -fno-exceptions -std=c++14
  LOCAL_CONLYFLAGS := -std=c11
  include $(BUILD_SHARED_LIBRARY)
endif

$(call import-module,android/native_app_glue)

all: $(LOCAL_INSTALLED)