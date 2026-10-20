# Material Color Utilities 的子集（Apache-2.0，见同目录 README.md）：colortone.cpp 用它从强调色生成
# Material You 配色。应用（src/CMakeLists.txt）与截图工具（scripts/qmlshot）都 include 这一份
if(NOT TARGET material_color_utilities)
    set(_mcu ${CMAKE_CURRENT_LIST_DIR}/cpp)
    add_library(material_color_utilities STATIC
        ${_mcu}/blend/blend.cc
        ${_mcu}/cam/cam.cc
        ${_mcu}/cam/hct.cc
        ${_mcu}/cam/hct_solver.cc
        ${_mcu}/cam/viewing_conditions.cc
        ${_mcu}/contrast/contrast.cc
        ${_mcu}/dislike/dislike.cc
        ${_mcu}/dynamiccolor/dynamic_color.cc
        ${_mcu}/dynamiccolor/dynamic_scheme.cc
        ${_mcu}/dynamiccolor/material_dynamic_colors.cc
        ${_mcu}/palettes/tones.cc
        ${_mcu}/scheme/scheme_tonal_spot.cc
        ${_mcu}/utils/utils.cc)
    # 上游的头文件按 "cpp/..." 引用
    target_include_directories(material_color_utilities PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_features(material_color_utilities PUBLIC cxx_std_17)
    set_target_properties(material_color_utilities PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
