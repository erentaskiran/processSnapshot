# Google Test'i indir (FetchContent ile)
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

# Test dosyaları
set(TEST_SOURCES
    test_serializer.cpp
    test_state_manager.cpp
    test_logger.cpp
    test_rollback.cpp
    test_integration.cpp
)

# Test executable
add_executable(checkpoint_tests ${TEST_SOURCES})

target_link_libraries(checkpoint_tests PRIVATE
    state_checkpoint
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

# CTest entegrasyonu
include(GoogleTest)
gtest_discover_tests(checkpoint_tests)
