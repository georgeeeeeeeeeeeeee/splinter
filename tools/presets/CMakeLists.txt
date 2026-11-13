# fault-presets: runs the real plug-in processor offline. Checks every factory preset (loads strictly,
# renders a file through it, reports level and NaN/Inf) and can snapshot the editor to a PNG.
juce_add_console_app(FaultPresets PRODUCT_NAME "fault-presets")
target_sources(FaultPresets PRIVATE main.cpp)
target_compile_definitions(FaultPresets PRIVATE JucePlugin_Name="Fault" JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(FaultPresets PRIVATE fault_plugin_code juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
