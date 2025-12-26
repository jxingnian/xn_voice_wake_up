# xn_voice_wake 组件 CMakeLists.txt

# Edge Impulse SDK 源文件
file(GLOB_RECURSE EI_SDK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/classifier/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/dsp/dct/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/dsp/kissfft/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/dsp/memory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/c/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/core/c/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/core/api/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/kernels/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/kernels/internal/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/micro/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/micro/kernels/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/micro/memory_planner/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/tflite-model/*.cpp"
)

# 组件源文件
set(COMPONENT_SRCS
    "src/voice_wake_module.c"
    "src/audio_processor.c"
    "src/detection_engine.cpp"
    ${EI_SDK_SOURCES}
)

# 包含目录
set(COMPONENT_INCLUDE_DIRS
    "include"
    "."
    "edge-impulse-sdk"
    "edge-impulse-sdk/classifier"
    "edge-impulse-sdk/dsp"
    "edge-impulse-sdk/porting"
    "edge-impulse-sdk/tensorflow"
    "edge-impulse-sdk/third_party"
    "edge-impulse-sdk/third_party/flatbuffers/include"
    "edge-impulse-sdk/third_party/gemmlowp"
    "edge-impulse-sdk/third_party/ruy"
    "model-parameters"
    "tflite-model"
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS ${COMPONENT_INCLUDE_DIRS}
    REQUIRES driver esp_timer freertos
)

# 编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=maybe-uninitialized
    -Wno-error=sign-compare
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=type-limits
    -Wno-error=strict-aliasing
)

# 定义 Edge Impulse 相关宏
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EI_PORTING_ESPRESSIF=1
    EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1
)
