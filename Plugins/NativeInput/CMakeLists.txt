set(SOURCES 
    "Include/Babylon/Plugins/NativeInput.h"
    "Source/Shared/NativeInput.cpp"
    "Source/Shared/NativeInput.h"
    "Source/Shared/DeviceInputSystem.cpp"
    "Source/Shared/DeviceInputSystem.h")

if(ANDROID)
    set(SOURCES ${SOURCES}
        "Source/Android/NativeInput.cpp")
elseif(IOS)
    set(SOURCES ${SOURCES}
        "Source/iOS/NativeInput.cpp")
elseif(APPLE)
    set(SOURCES ${SOURCES}
         "Source/macOS/NativeInput.cpp")
elseif(UNIX)
	set(SOURCES ${SOURCES}
         "Source/Unix/NativeInput.cpp")
else()
    set(SOURCES ${SOURCES}
        "Source/Windows/NativeInput.cpp")
endif()

add_library(NativeInput ${SOURCES})
warnings_as_errors(NativeInput)

target_include_directories(NativeInput PUBLIC "Include")

target_link_libraries(NativeInput
    PUBLIC napi
    PRIVATE JsRuntimeInternal
    PRIVATE arcana)

set_property(TARGET NativeInput PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
