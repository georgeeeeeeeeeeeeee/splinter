include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# DSP tests link only fault_dsp: proves the engine builds and runs without JUCE.
add_executable(fault_tests stage1_tests.cpp stage2_tests.cpp stage3_tests.cpp stage4_tests.cpp stage5_tests.cpp)
target_link_libraries(fault_tests PRIVATE fault_dsp Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(fault_tests)
