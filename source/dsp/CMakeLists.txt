# Pure C++20 DSP engine. Must never depend on JUCE (keeps the WASM route open, brief §1).
add_library(fault_dsp STATIC
    Params.cpp
    RecordBuffer.cpp
    Clock.cpp
    Engine.cpp)
target_include_directories(fault_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(fault_dsp PUBLIC cxx_std_20)
