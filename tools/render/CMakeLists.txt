# Offline renderer: a WAV through the DSP engine with a JSON file of params, tempo and automation;
# writes the output WAV, a PNG waveform (ticks + slices marked) and a tick log (CSV). Mac + Windows.
juce_add_console_app(FaultRender PRODUCT_NAME "fault-render")
target_sources(FaultRender PRIVATE main.cpp)
target_compile_definitions(FaultRender PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(FaultRender PRIVATE fault_dsp juce::juce_audio_formats juce::juce_graphics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
