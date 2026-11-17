if(NOT WIN32)
    message(STATUS "eat-app is Windows-only; skipping")
    return()
endif()

if(NOT EAT_WEBVIEW2_FOUND)
    find_path(WEBVIEW2_INCLUDE_DIR
        NAMES WebView2.h
        HINTS "${WEBVIEW2_ROOT}/build/native/include"
              "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/webview2/include"
    )
    find_library(WEBVIEW2_LOADER_LIB
        NAMES WebView2LoaderStatic
        HINTS "${WEBVIEW2_ROOT}/build/native/x64"
              "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/webview2/lib"
    )
    if(WEBVIEW2_INCLUDE_DIR AND WEBVIEW2_LOADER_LIB)
        set(EAT_WEBVIEW2_FOUND TRUE CACHE INTERNAL "")
    endif()
endif()

if(NOT EAT_WEBVIEW2_FOUND)
    message(STATUS "eat-app skipped: set WEBVIEW2_ROOT to the WebView2 SDK")
    return()
endif()

add_executable(eat-app WIN32
    win32/main.cc
    "${CMAKE_SOURCE_DIR}/client/transport/TcpTransport.cc"
)
target_include_directories(eat-app PRIVATE "${WEBVIEW2_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}")
target_link_libraries(eat-app PRIVATE
    eat_protocol
    "${WEBVIEW2_LOADER_LIB}"
    ole32 oleaut32 user32 shell32 shlwapi ws2_32
)

# The page is shipped with the installer; the App never loads a remote first
# screen. Sources stay at the repo root so browser development keeps working.
set(EAT_WEB_OUTPUT "$<TARGET_FILE_DIR:eat-app>/web")
add_custom_command(TARGET eat-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EAT_WEB_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/index.html"
        "${CMAKE_SOURCE_DIR}/app.js"
        "${CMAKE_SOURCE_DIR}/bridge.js"
        "${CMAKE_SOURCE_DIR}/agent-adapter.js"
        "${CMAKE_SOURCE_DIR}/styles.css"
        "${CMAKE_SOURCE_DIR}/features.css"
        "${CMAKE_SOURCE_DIR}/agent.css"
        "${EAT_WEB_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "${EAT_WEB_OUTPUT}/assets"
    COMMENT "Bundling local web assets for the Windows App"
)
