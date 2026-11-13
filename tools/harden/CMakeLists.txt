# fault-harden (Stage 7): state round trips, the random-automation fuzz and the CPU benchmark, on the
# real plug-in processor.
juce_add_console_app(FaultHarden PRODUCT_NAME "fault-harden")
target_sources(FaultHarden PRIVATE main.cpp)
target_compile_definitions(FaultHarden PRIVATE JucePlugin_Name="Fault" JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(FaultHarden PRIVATE fault_plugin_code juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
