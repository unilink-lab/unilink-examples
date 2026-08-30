include(FetchContent)

set(WIRESTEAD_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE
)
set(WIRESTEAD_BUILD_DOCS
    OFF
    CACHE BOOL "" FORCE
)
set(WIRESTEAD_ENABLE_INSTALL
    OFF
    CACHE BOOL "" FORCE
)

FetchContent_Declare(
  wirestead
  GIT_REPOSITORY https://github.com/wirestead/wirestead.git
  GIT_TAG v0.9.6
)

FetchContent_MakeAvailable(wirestead)
