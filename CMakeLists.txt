cmake_minimum_required(VERSION 3.16)
project(libedgai VERSION 0.2.0 DESCRIPTION "EduOS AI engine library" LANGUAGES C CXX)

# llama.cpp requires C++17 in its own build; we only need C11 for libedgai itself
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build options ────────────────────────────────────────────────────────────

# Voice pipeline (Vosk STT + Piper TTS + ALSA) — Linux desktop only.
# Requires: Debian bookworm, libasound2-dev, libvosk.so 0.3.45 in vendor/vosk/,
#           and vendor/piper submodule initialised at d6975e2.
# Disable to build a text-only library that compiles without ALSA headers.
option(EDGAI_VOICE
    "Enable voice pipeline: Vosk STT + Piper TTS + ALSA (Debian bookworm only)"
    OFF)

# Emergency socket fallback — uses Phase 3 engine.py instead of llama.cpp.
option(EDGAI_SOCKET_FALLBACK
    "Use Python engine.py socket proxy instead of llama.cpp (emergency fallback only)"
    OFF)

# ── llama.cpp submodule ──────────────────────────────────────────────────────
# Pinned commit: 0ed235ea2c17a19fc8238668653946721ed136fd (gguf-v0.19.0-781-g0ed235ea2)
# Update pin intentionally; never do so automatically (API breaks regularly).

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_SUBMOD_RESULT
    )
    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(FATAL_ERROR "git submodule update failed. Clone with --recurse-submodules.")
    endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/llama.cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "vendor/llama.cpp not found. Run:\n"
        "  git submodule update --init --recursive")
endif()

# Disable llama.cpp build targets we don't need — dramatically reduces build time
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)

# Metal is macOS-only — disable it on Linux and Android to avoid build errors
if(NOT APPLE)
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(vendor/llama.cpp)

# ── SQLite ───────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# ── Voice pipeline dependencies (EDGAI_VOICE=ON only) ───────────────────────
if(EDGAI_VOICE)
    # ALSA — Debian: libasound2-dev
    pkg_check_modules(ALSA REQUIRED alsa)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR
            "ALSA not found. Install libasound2-dev:\n"
            "  sudo apt-get install libasound2-dev")
    endif()

    # Vosk 0.3.45 prebuilt shared library.
    # Download from:
    #   wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-x86_64-0.3.45.zip
    #   unzip vosk-linux-x86_64-0.3.45.zip
    #   cp vosk-linux-x86_64-0.3.45/libvosk.so vendor/vosk/
    set(VOSK_LIB_PATH "${PROJECT_SOURCE_DIR}/vendor/vosk/libvosk.so")
    if(NOT EXISTS "${VOSK_LIB_PATH}")
        message(FATAL_ERROR
            "vendor/vosk/libvosk.so not found.\n"
            "Download Vosk 0.3.45 (amd64) and copy libvosk.so to vendor/vosk/.\n"
            "See vendor/vosk/vosk_api.h for download instructions.\n"
            "Or build without voice: cmake .. -DEDGAI_VOICE=OFF")
    endif()

    add_library(vosk SHARED IMPORTED)
    set_target_properties(vosk PROPERTIES
        IMPORTED_LOCATION "${VOSK_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/vendor/vosk"
    )

    # Piper TTS — git submodule OHF-Voice/piper1-gpl @ d6975e2
    # Initialise with: git submodule update --init vendor/piper
    set(PIPER_ROOT "${PROJECT_SOURCE_DIR}/vendor/piper")
    if(NOT EXISTS "${PIPER_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "vendor/piper submodule not initialised.\n"
            "Run: git submodule update --init vendor/piper\n"
            "Or build without voice: cmake .. -DEDGAI_VOICE=OFF")
    endif()

    # Build only the libpiper core; disable samples and tests
    set(PIPER_BUILD_EXE    OFF CACHE BOOL "" FORCE)
    set(PIPER_BUILD_SHARED ON  CACHE BOOL "" FORCE)
    add_subdirectory("${PIPER_ROOT}" piper_build)

    message(STATUS "edgai: voice pipeline ENABLED (Vosk + Piper + ALSA)")
else()
    message(STATUS "edgai: voice pipeline DISABLED (text-only build)")
endif()

# ── libedgai source files ────────────────────────────────────────────────────
set(EDGAI_SOURCES
    # Core — Phase 3+4 implementations
    src/core/session.c
    src/core/query.c
    src/core/profile.c
    src/core/state.c

    # RAG — Phase 4 implementations
    src/rag/retriever.c
    src/rag/preprocessor.c
    src/rag/ranker.c
    src/rag/formatter.c

    # Inference — Phase 4 implementations
    src/inference/llama_backend.c
    src/inference/model_loader.c
    src/inference/tier.c

    # Voice — Phase 5 (always compiled; ALSA/Vosk/Piper code gated on EDGAI_VOICE_ENABLED)
    src/voice/audio_capture.c
    src/voice/vad.c
    src/voice/sanitize.c
    src/voice/transcribe.c
    src/voice/speak.c

    # Stubs — phases 6–7
    src/core/cache.c
    src/rag/embedder.c
    src/dbus/signals.c
    bindings/android/jni/eduos_jni.c
)

add_library(edgai SHARED ${EDGAI_SOURCES})

target_include_directories(edgai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/inference
        ${CMAKE_CURRENT_SOURCE_DIR}/src/voice
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/llama.cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/llama.cpp/ggml/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vosk
        ${SQLITE3_INCLUDE_DIRS}
)

# Link against llama target provided by add_subdirectory(vendor/llama.cpp)
target_link_libraries(edgai
    PRIVATE
        llama
        ${SQLITE3_LIBRARIES}
        m
)

target_compile_options(edgai PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(edgai PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "edgai"
)

# ── Voice-specific compile definitions and linking ───────────────────────────
if(EDGAI_VOICE)
    target_compile_definitions(edgai PRIVATE EDGAI_VOICE_ENABLED=1)

    target_include_directories(edgai PRIVATE
        ${ALSA_INCLUDE_DIRS}
        ${PIPER_ROOT}/libpiper
    )

    target_link_libraries(edgai PRIVATE
        vosk
        piper
        ${ALSA_LIBRARIES}
    )

    # Ensure libvosk.so is in the rpath at install time
    set_target_properties(edgai PROPERTIES
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:${PROJECT_SOURCE_DIR}/vendor/vosk"
    )
endif()

# ── Socket fallback compile option ───────────────────────────────────────────
if(EDGAI_SOCKET_FALLBACK)
    target_compile_definitions(edgai PRIVATE EDGAI_SOCKET_FALLBACK=1)
    message(STATUS "edgai: socket fallback ENABLED — using engine.py, NOT llama.cpp")
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

function(add_edgai_test name)
    add_executable(${name} tests/${name}.c)
    # Link sqlite3 explicitly: edgai links it PRIVATE so it doesn't
    # propagate to test executables that call sqlite3 functions directly.
    target_link_libraries(${name} PRIVATE edgai ${SQLITE3_LIBRARIES})
    target_include_directories(${name} PRIVATE
        include
        src/voice
        ${SQLITE3_INCLUDE_DIRS}
    )
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_edgai_test(test_session)
add_edgai_test(test_query)
add_edgai_test(test_rag)
add_edgai_test(test_voice)
add_edgai_test(test_inference)
add_edgai_test(test_retriever)
add_edgai_test(test_state)
add_edgai_test(test_preprocessor)

# test_voice also needs audio_capture.h for EDGAI_CAPTURE_RATE constant
target_include_directories(test_voice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/voice
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vosk
)
if(EDGAI_VOICE)
    target_compile_definitions(test_voice PRIVATE EDGAI_VOICE_ENABLED=1)
    target_include_directories(test_voice PRIVATE ${ALSA_INCLUDE_DIRS})
endif()

# ── Tools ────────────────────────────────────────────────────────────────────
add_executable(edgai-chat tools/chat.c)
target_link_libraries(edgai-chat PRIVATE edgai)
target_include_directories(edgai-chat PRIVATE include)
target_compile_options(edgai-chat PRIVATE -Wall -Wextra -Wpedantic)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS edgai
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/edgai DESTINATION include)

if(EDGAI_VOICE)
    install(FILES "${VOSK_LIB_PATH}" DESTINATION lib)
endif()
