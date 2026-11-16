# Vendored C dependencies, built as static libraries.

# ── SQLite ───────────────────────────────────────────────────────────────────
add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC sqlite)
target_compile_definitions(sqlite3
    PUBLIC
        SQLITE_OMIT_LOAD_EXTENSION
    PRIVATE
        SQLITE_THREADSAFE=2
        SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_LIKE_DOESNT_MATCH_BLOBS
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_SHARED_CACHE
        SQLITE_USE_ALLOCA
        SQLITE_ENABLE_MATH_FUNCTIONS
        SQLITE_ENABLE_FTS5)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# ── miniz ────────────────────────────────────────────────────────────────────
add_library(miniz STATIC miniz/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC miniz)
if(MSVC)
    target_compile_options(miniz PRIVATE /W0)
else()
    target_compile_options(miniz PRIVATE -w)
endif()
