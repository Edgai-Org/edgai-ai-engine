cmake_minimum_required(VERSION 3.16)
project(libedgai VERSION 0.2.0 DESCRIPTION "EduOS AI engine library" LANGUAGES C CXX)

# llama.cpp requires C++17 in its own build; we only need C11 for libedgai itself
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

    # Stubs — phases 5–7
    src/core/cache.c
    src/rag/embedder.c
    src/voice/transcribe.c
    src/voice/speak.c
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
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/llama.cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/llama.cpp/ggml/include
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

# ── Socket fallback compile option ───────────────────────────────────────────
# Emergency only — uses Phase 3 engine.py socket proxy instead of llama.cpp.
# Enable with: cmake .. -DEDGAI_SOCKET_FALLBACK=ON
option(EDGAI_SOCKET_FALLBACK
    "Use Python engine.py socket proxy instead of llama.cpp (emergency fallback only)"
    OFF)
if(EDGAI_SOCKET_FALLBACK)
    target_compile_definitions(edgai PRIVATE EDGAI_SOCKET_FALLBACK=1)
    message(STATUS "Edgai: socket fallback ENABLED — using engine.py, NOT llama.cpp")
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
