cmake_minimum_required(VERSION 3.16)
project(libeduos VERSION 0.1.0 DESCRIPTION "EduOS AI engine library" LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Core sources — Phase 3: real implementations
set(CORE_SOURCES
    src/core/session.c
    src/core/query.c
    src/core/profile.c
    src/core/cache.c
)

# Stub sources — phases 4–6
set(STUB_SOURCES
    src/rag/retriever.c
    src/rag/embedder.c
    src/rag/ranker.c
    src/rag/preprocessor.c
    src/rag/formatter.c
    src/inference/model_loader.c
    src/inference/llama_backend.c
    src/inference/tier.c
    src/voice/transcribe.c
    src/voice/speak.c
    src/dbus/signals.c
    bindings/android/jni/eduos_jni.c
)

add_library(eduos SHARED ${CORE_SOURCES} ${STUB_SOURCES})

target_include_directories(eduos
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(eduos PRIVATE ${SQLITE3_LIBRARIES})

target_compile_options(eduos PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(eduos PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "eduos"
)

enable_testing()

add_executable(test_session tests/test_session.c)
target_link_libraries(test_session PRIVATE eduos)
target_include_directories(test_session PRIVATE include)
add_test(NAME test_session COMMAND test_session)

add_executable(test_query tests/test_query.c)
target_link_libraries(test_query PRIVATE eduos)
target_include_directories(test_query PRIVATE include)
add_test(NAME test_query COMMAND test_query)

install(TARGETS eduos
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/eduos DESTINATION include)
