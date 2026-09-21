cmake_minimum_required(VERSION 3.20)

project(VoxType LANGUAGES CXX RC)

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(VOXTYPE_RESOURCE_HEADER "${CMAKE_SOURCE_DIR}/src/app/resource.h")
file(READ "${VOXTYPE_RESOURCE_HEADER}" _voxtype_resource_header)
foreach(_part IN ITEMS MAJOR MINOR PATCH)
    string(REGEX MATCH
        "#define[ \\t]+APP_VERSION_${_part}[ \\t]+([0-9]+)"
        _voxtype_version_match
        "${_voxtype_resource_header}")
    if(NOT DEFINED CMAKE_MATCH_1 OR "${CMAKE_MATCH_1}" STREQUAL "")
        message(FATAL_ERROR "Could not read APP_VERSION_${_part} from ${VOXTYPE_RESOURCE_HEADER}")
    endif()
    set(VOXTYPE_VERSION_${_part} "${CMAKE_MATCH_1}")
endforeach()
set(VOXTYPE_PRODUCT_VERSION
    "${VOXTYPE_VERSION_MAJOR}.${VOXTYPE_VERSION_MINOR}.${VOXTYPE_VERSION_PATCH}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VOXTYPE_RESOURCE_HEADER}")
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/voxtype-version.txt"
    CONTENT "${VOXTYPE_PRODUCT_VERSION}\n")

set(SHERPA_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx")
set(KNF_DIR "${CMAKE_SOURCE_DIR}/third_party/kaldi_native_fbank")
set(OPUS_DIR "${CMAKE_SOURCE_DIR}/third_party/opus")

add_executable(VoxType WIN32
    src/app/main.cpp
    src/app/main_window.cpp
    src/app/recording_session_controller.cpp
    src/app/asr_attempt_manager.cpp
    src/app/asr_probe_service_impl.cpp
    src/ui/hud_pagination.cpp
    src/app/resources.rc
    src/core/path_service.cpp
    src/core/debug_logger.cpp
    src/core/config_store.cpp
    src/core/config_registry.cpp
    src/core/vocabulary_manager.cpp
    src/core/asr_probe_service.cpp
    src/core/app_state.cpp
    src/audio/audio_capture.cpp
    src/audio/audio_chunk_sink.cpp
    src/audio/audio_diagnostics.cpp
    src/audio/batch_vad_trimmer.cpp
    src/audio/streaming_vad_trimmer.cpp
    src/audio/vad_trim_core.cpp
    src/audio/wasapi_capture.cpp
    src/asr/engine_local.cpp
    src/asr/asr_session.cpp
    src/asr/asr_diagnostics.cpp
    src/asr/asr_result.cpp
    src/asr/asr_dispatcher.cpp
    src/asr/asr_runtime_log.cpp
    src/asr/cloud_asr_common.cpp
    src/asr/cloud_http_common.cpp
    src/asr/doubao_ime_asr.cpp
    src/asr/doubao_ime_streaming_session.cpp
    src/asr/mai_transcribe.cpp
    src/asr/mimo_asr.cpp
    src/asr/qwen_asr.cpp
    src/asr/qwen_audio_http.cpp
    src/asr/qwen_audio_streaming.cpp
    src/asr/qwen_audio_streaming_session.cpp
    src/asr/qwen_streaming_session.cpp
    src/asr/winhttp_websocket_transport.cpp
    src/asr/qwen_free_streaming_session.cpp
    src/asr/qwen_free_proto_utdid.cpp
    src/asr/qwen_free_proto_asr.cpp
    src/asr/qwen_free_proto_llm.cpp
    src/asr/qwen_free_proto_unet.cpp
    src/asr/volcengine_net_diag.cpp
    src/asr/volcengine_streaming_session.cpp
    src/asr/asr_metrics.cpp
    src/core/startup_registration.cpp
    src/ui/hud.cpp
    src/ui/hotkey.cpp
    src/ui/form_builder.cpp
    src/ui/settings.cpp
    src/ui/settings_controls.cpp
    src/ui/settings_dialogs.cpp
    src/ui/ui_theme.cpp
    src/ui/tabs/tab_general.cpp
    src/ui/tabs/tab_speech_engine.cpp
    src/ui/tabs/tab_advanced.cpp
    src/ui/tabs/tab_llm.cpp
    src/ui/tabs/tab_vocabulary.cpp
    src/ui/providers/provider_baidu.cpp
    src/ui/providers/provider_volcengine.cpp
    src/ui/providers/provider_local.cpp
    src/ui/providers/provider_qwen.cpp
    src/ui/providers/qwen_settings_helper.cpp
    src/ui/providers/provider_mimo.cpp
    src/ui/providers/provider_doubao.cpp
    src/ui/providers/provider_qwen_free.cpp
    src/ui/providers/provider_mai.cpp
    src/platform/text_injector.cpp
)

target_compile_definitions(VoxType PRIVATE UNICODE _UNICODE)
target_include_directories(VoxType PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app"
    "${CMAKE_SOURCE_DIR}/src/asr"
    "${CMAKE_SOURCE_DIR}/src/audio"
    "${CMAKE_SOURCE_DIR}/src/ui"
    "${CMAKE_SOURCE_DIR}/src/ui/tabs"
    "${CMAKE_SOURCE_DIR}/src/ui/providers"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/platform"
    "${SHERPA_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include"
    "${KNF_DIR}/include"
    "${OPUS_DIR}/include"
)
target_link_directories(VoxType PRIVATE
    "${SHERPA_DIR}/lib"
    "${CMAKE_SOURCE_DIR}/third_party/onnxruntime"
    "${KNF_DIR}/lib"
    "${OPUS_DIR}/lib"
)
target_link_libraries(VoxType PRIVATE
    sherpa-onnx-cxx-api
    onnxruntime
    kaldi-native-fbank-core
    user32
    gdi32
    shell32
    ole32
    oleaut32
    oleacc
    uiautomationcore
    comctl32
    d2d1
    dwrite
    shlwapi
    winmm
    winhttp
    ws2_32
    crypt32
    bcrypt
    rpcrt4
    mmdevapi
    imm32
    delayimp
    advapi32
    opus
)

if(MSVC)
    target_compile_options(VoxType PRIVATE /O2 /EHsc /utf-8)
    target_link_options(VoxType PRIVATE
        /MANIFEST:NO
        /DELAYLOAD:sherpa-onnx-cxx-api.dll
        /DELAYLOAD:onnxruntime.dll
        /DELAYLOAD:kaldi-native-fbank-core.dll
    )
endif()

include("${CMAKE_SOURCE_DIR}/cmake/VoxTypeRuntime.cmake")
voxtype_install_runtime(VoxType)

# Offline protocol regression target.  It deliberately links only the pure
# JSON/HMAC pieces, writes outside the runtime payload, and is built only when
# explicitly requested by build.bat --test. The generic HMAC implementation
# is not linked into the production VoxType executable.
add_executable(qwen_free_protocol_test
    tests/qwen_free_protocol_test.cpp
    src/asr/qwen_free_proto_sign.cpp
)
target_include_directories(qwen_free_protocol_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/asr"
    "${CMAKE_SOURCE_DIR}/src/core"
)
if(MSVC)
    target_compile_options(qwen_free_protocol_test PRIVATE /O2 /EHsc /utf-8)
endif()
set_target_properties(qwen_free_protocol_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME qwen_free_protocol_test COMMAND qwen_free_protocol_test)

add_executable(qwen_audio_json_test
    tests/qwen_audio_json_test.cpp
    src/asr/qwen_asr.cpp
    src/asr/qwen_audio_http.cpp
    src/asr/qwen_audio_streaming.cpp
    src/asr/winhttp_websocket_transport.cpp
    src/asr/cloud_asr_common.cpp
    src/asr/cloud_http_common.cpp
)
target_compile_definitions(qwen_audio_json_test PRIVATE VOXTYPE_QWEN_AUDIO_PROTOCOL_TEST=1)
target_include_directories(qwen_audio_json_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/asr"
    "${CMAKE_SOURCE_DIR}/src/core"
)
if(MSVC)
    target_compile_options(qwen_audio_json_test PRIVATE /O2 /EHsc /utf-8)
endif()
target_link_libraries(qwen_audio_json_test PRIVATE winhttp crypt32 bcrypt ole32 ws2_32)
set_target_properties(qwen_audio_json_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME qwen_audio_json_test COMMAND qwen_audio_json_test)

add_executable(llm_refine_test
    tests/llm_refine_test.cpp
)
target_include_directories(llm_refine_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/core"
)
if(MSVC)
    target_compile_options(llm_refine_test PRIVATE /O2 /EHsc /utf-8)
endif()
target_link_libraries(llm_refine_test PRIVATE winhttp crypt32)
set_target_properties(llm_refine_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME llm_refine_test COMMAND llm_refine_test)

add_executable(audio_diagnostics_test
    tests/audio_diagnostics_test.cpp
    src/audio/audio_diagnostics.cpp
)
target_compile_definitions(audio_diagnostics_test PRIVATE UNICODE _UNICODE)
target_include_directories(audio_diagnostics_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/audio"
    "${CMAKE_SOURCE_DIR}/src/app"
)
if(MSVC)
    target_compile_options(audio_diagnostics_test PRIVATE /O2 /EHsc /utf-8)
endif()
target_link_libraries(audio_diagnostics_test PRIVATE bcrypt shell32)
set_target_properties(audio_diagnostics_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME audio_diagnostics_test COMMAND audio_diagnostics_test)

# Offline protocol regression tests for shared JSON, Baidu and Volcengine helpers.
add_executable(asr_json_protocol_test
    tests/asr_json_protocol_test.cpp
    src/audio/audio_diagnostics.cpp
    src/asr/asr_result.cpp
    src/asr/cloud_http_common.cpp
    src/asr/mai_transcribe.cpp
    src/core/config_registry.cpp
    src/core/path_service.cpp
    src/core/vocabulary_manager.cpp
)
target_compile_definitions(asr_json_protocol_test PRIVATE
    UNICODE _UNICODE VOXTYPE_MAI_PROTOCOL_TEST=1)
target_include_directories(asr_json_protocol_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/asr"
    "${CMAKE_SOURCE_DIR}/src/audio"
)
if(MSVC)
    target_compile_options(asr_json_protocol_test PRIVATE /O2 /EHsc /utf-8)
endif()
target_link_libraries(asr_json_protocol_test PRIVATE
    winhttp crypt32 rpcrt4 bcrypt shell32)
set_target_properties(asr_json_protocol_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME asr_json_protocol_test COMMAND asr_json_protocol_test)

add_executable(hud_pagination_test
    tests/hud_pagination_test.cpp
    src/ui/hud_pagination.cpp
)
target_include_directories(hud_pagination_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui"
    "${CMAKE_SOURCE_DIR}/src/core"
)
if(MSVC)
    target_compile_options(hud_pagination_test PRIVATE /O2 /EHsc /utf-8)
endif()
set_target_properties(hud_pagination_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tests"
)
add_test(NAME hud_pagination_test COMMAND hud_pagination_test)

# Developer-only diagnostic replay utility. It reuses the production ASR
# sessions but remains outside the canonical runtime payload and normal build.
add_executable(asr_audio_replay EXCLUDE_FROM_ALL
    tools/asr_audio_replay.cpp
    src/core/path_service.cpp
    src/core/debug_logger.cpp
    src/core/config_store.cpp
    src/core/config_registry.cpp
    src/core/vocabulary_manager.cpp
    src/audio/audio_capture.cpp
    src/audio/audio_chunk_sink.cpp
    src/audio/audio_diagnostics.cpp
    src/audio/batch_vad_trimmer.cpp
    src/audio/streaming_vad_trimmer.cpp
    src/audio/vad_trim_core.cpp
    src/audio/wasapi_capture.cpp
    src/asr/engine_local.cpp
    src/asr/asr_session.cpp
    src/asr/asr_diagnostics.cpp
    src/asr/asr_result.cpp
    src/asr/asr_dispatcher.cpp
    src/asr/asr_runtime_log.cpp
    src/asr/cloud_asr_common.cpp
    src/asr/cloud_http_common.cpp
    src/asr/doubao_ime_asr.cpp
    src/asr/doubao_ime_streaming_session.cpp
    src/asr/mai_transcribe.cpp
    src/asr/mimo_asr.cpp
    src/asr/qwen_asr.cpp
    src/asr/qwen_audio_http.cpp
    src/asr/qwen_audio_streaming.cpp
    src/asr/qwen_audio_streaming_session.cpp
    src/asr/qwen_streaming_session.cpp
    src/asr/winhttp_websocket_transport.cpp
    src/asr/qwen_free_streaming_session.cpp
    src/asr/qwen_free_proto_utdid.cpp
    src/asr/qwen_free_proto_asr.cpp
    src/asr/qwen_free_proto_llm.cpp
    src/asr/qwen_free_proto_unet.cpp
    src/asr/volcengine_net_diag.cpp
    src/asr/volcengine_streaming_session.cpp
)
target_compile_definitions(asr_audio_replay PRIVATE UNICODE _UNICODE)
target_include_directories(asr_audio_replay PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app"
    "${CMAKE_SOURCE_DIR}/src/asr"
    "${CMAKE_SOURCE_DIR}/src/audio"
    "${CMAKE_SOURCE_DIR}/src/ui"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${SHERPA_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include"
    "${KNF_DIR}/include"
    "${OPUS_DIR}/include"
)
target_link_directories(asr_audio_replay PRIVATE
    "${SHERPA_DIR}/lib"
    "${CMAKE_SOURCE_DIR}/third_party/onnxruntime"
    "${KNF_DIR}/lib"
    "${OPUS_DIR}/lib"
)
target_link_libraries(asr_audio_replay PRIVATE
    sherpa-onnx-cxx-api
    onnxruntime
    kaldi-native-fbank-core
    user32
    shell32
    ole32
    winmm
    winhttp
    ws2_32
    crypt32
    bcrypt
    mmdevapi
    delayimp
    advapi32
    opus
)
if(MSVC)
    target_compile_options(asr_audio_replay PRIVATE /O2 /EHsc /utf-8)
    target_link_options(asr_audio_replay PRIVATE
        /DELAYLOAD:sherpa-onnx-cxx-api.dll
        /DELAYLOAD:onnxruntime.dll
        /DELAYLOAD:kaldi-native-fbank-core.dll
    )
endif()
set_target_properties(asr_audio_replay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../artifacts/tools"
)
