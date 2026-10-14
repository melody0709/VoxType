# Canonical Runtime install rules. The build tree is never a supported runtime
# directory; build.bat installs this component into build/run/x64-release.

function(voxtype_install_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "voxtype_install_runtime: target '${target}' does not exist")
    endif()

    set(_src "${CMAKE_SOURCE_DIR}")
    set(_runtime_files
        "${_src}/dll/sherpa-onnx-cxx-api.dll"
        "${_src}/dll/sherpa-onnx-c-api.dll"
        "${_src}/dll/onnxruntime.dll"
        "${_src}/dll/kaldi-native-fbank-core.dll"
        "${_src}/models/silero_vad.int8.onnx"
        "${_src}/models/fireredvad_stream_vad_with_cache.onnx"
        "${_src}/dll/aria2c.exe"
        "${_src}/download_models.ps1"
        "${_src}/README.md"
        "${_src}/doc/README_zh.md"
        "${_src}/doc/volcengine/volcengine_asr_guide_zh.md"
        # Browser companion payload: the MV3 extension the user loads unpacked,
        # the native messaging host manifest the browser reads, and the
        # registration helper that owns the two HKCU values.
        "${_src}/extensions/browser-bridge/manifest.json"
        "${_src}/extensions/browser-bridge/worker.js"
        "${_src}/extensions/browser-bridge/README.md"
        "${_src}/extensions/browser-bridge/icons/icon-16.png"
        "${_src}/extensions/browser-bridge/icons/icon-32.png"
        "${_src}/extensions/browser-bridge/icons/icon-48.png"
        "${_src}/extensions/browser-bridge/icons/icon-128.png"
        "${_src}/packaging/native-messaging/com.voxtype.browser_bridge.json"
        "${_src}/tools/register_browser_bridge.ps1"
    )
    foreach(_required IN LISTS _runtime_files)
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR "Missing required VoxType runtime input: ${_required}")
        endif()
    endforeach()

    set(CMAKE_INSTALL_MESSAGE NEVER)

    # All mutable data belongs in LocalAppData (or in a Portable payload). The
    # development runtime may therefore be refreshed exactly on every install.
    install(CODE [=[
        set(_runtime_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        file(REMOVE_RECURSE
            "${_runtime_root}/models"
            "${_runtime_root}/doc"
            "${_runtime_root}/docs"
            "${_runtime_root}/extensions"
            "${_runtime_root}/native-messaging"
            "${_runtime_root}/scripts")
        file(REMOVE
            "${_runtime_root}/VoxType.exe"
            "${_runtime_root}/sherpa-onnx-cxx-api.dll"
            "${_runtime_root}/sherpa-onnx-c-api.dll"
            "${_runtime_root}/onnxruntime.dll"
            "${_runtime_root}/kaldi-native-fbank-core.dll"
            "${_runtime_root}/aria2c.exe"
            "${_runtime_root}/download_models.ps1"
            "${_runtime_root}/README.md"
            "${_runtime_root}/runtime-manifest.json")
    ]=] COMPONENT Runtime)

    install(TARGETS ${target}
        RUNTIME DESTINATION "."
        COMPONENT Runtime)
    install(FILES
        "${_src}/dll/sherpa-onnx-cxx-api.dll"
        "${_src}/dll/sherpa-onnx-c-api.dll"
        "${_src}/dll/onnxruntime.dll"
        "${_src}/dll/kaldi-native-fbank-core.dll"
        DESTINATION "."
        COMPONENT Runtime)
    install(PROGRAMS "${_src}/dll/aria2c.exe"
        DESTINATION "."
        COMPONENT Runtime)
    install(FILES "${_src}/download_models.ps1" "${_src}/README.md"
        DESTINATION "."
        COMPONENT Runtime)
    install(FILES
        "${_src}/models/silero_vad.int8.onnx"
        "${_src}/models/fireredvad_stream_vad_with_cache.onnx"
        DESTINATION "models"
        COMPONENT Runtime)
    install(FILES "${_src}/doc/volcengine/volcengine_asr_guide_zh.md"
        DESTINATION "docs"
        COMPONENT Runtime)
    install(FILES "${_src}/doc/README_zh.md"
        DESTINATION "doc"
        COMPONENT Runtime)

    # Browser companion: the unpacked extension (the user points "Load unpacked"
    # at <runtime>/extensions/browser-bridge), the native messaging host manifest
    # whose relative path resolves to <runtime>/VoxType.exe, and the registration
    # helper. Nothing here is installed outside the runtime directory.
    install(DIRECTORY "${_src}/extensions/browser-bridge"
        DESTINATION "extensions"
        COMPONENT Runtime)
    install(FILES "${_src}/packaging/native-messaging/com.voxtype.browser_bridge.json"
        DESTINATION "native-messaging"
        COMPONENT Runtime)
    install(PROGRAMS "${_src}/tools/register_browser_bridge.ps1"
        DESTINATION "scripts"
        COMPONENT Runtime)

    set(_manifest_script "${CMAKE_CURRENT_BINARY_DIR}/WriteRuntimeManifest.cmake")
    configure_file(
        "${_src}/cmake/WriteRuntimeManifest.cmake.in"
        "${_manifest_script}"
        @ONLY)
    install(SCRIPT "${_manifest_script}" COMPONENT Runtime)
endfunction()
