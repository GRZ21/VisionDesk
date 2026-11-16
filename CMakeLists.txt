cmake_minimum_required(VERSION 3.16)

# ---------------- 基础 ----------------
set(APP_NAME VisionDesk)

project(${APP_NAME}
        VERSION 1.0.0
        LANGUAGES CXX
)

# ---------------- 版本信息 ----------------
set(VERSION_SUFFIX "" CACHE STRING "版本后缀 (beta/rc等)")

if (VERSION_SUFFIX)
    set(PROJECT_FULL_VERSION "${PROJECT_VERSION}-${VERSION_SUFFIX}")
else ()
    set(PROJECT_FULL_VERSION "${PROJECT_VERSION}")
endif ()

# ---------------- 构建信息 ----------------
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# ---------------- Git 信息 ----------------
find_package(Git QUIET)

set(GIT_HASH "unknown")
set(GIT_BRANCH "unknown")

if (GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )

    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
endif ()

# ---------------- Qt6 配置 ----------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
        Core
        Gui
        Widgets
        PrintSupport
        Sql
)

# ---------------- C++ 标准 ----------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- OpenCV ----------------
# 在 CMake 选项中设置 -DOpenCV_DIR=<包含 OpenCVConfig.cmake 的目录>
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# ---------------- ONNX Runtime ----------------
# 指向解压后的 SDK 根目录，其中应包含 include 和 lib 目录。
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime SDK root directory")
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS "${ONNXRUNTIME_ROOT}"
        PATH_SUFFIXES include include/onnxruntime/core/session
)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
        HINTS "${ONNXRUNTIME_ROOT}"
        PATH_SUFFIXES lib lib64
)
if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "ONNX Runtime SDK not found. Set -DONNXRUNTIME_ROOT=<SDK directory>.")
endif ()

add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
)
if (WIN32)
    get_filename_component(ONNXRUNTIME_LIBRARY_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    find_file(ONNXRUNTIME_DLL NAMES onnxruntime.dll
            HINTS "${ONNXRUNTIME_LIBRARY_DIR}" "${ONNXRUNTIME_ROOT}"
            PATH_SUFFIXES lib bin
    )
    if (NOT ONNXRUNTIME_DLL)
        message(FATAL_ERROR "onnxruntime.dll not found. Check ONNXRUNTIME_ROOT.")
    endif ()
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
            IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
    )
else ()
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
    )
endif ()

# ---------------- 源文件 ----------------
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        src/*.ui
        src/*.qrc
)

# IDE 目录结构
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SRC_FILES})

# ---------------- 可执行程序 ----------------
add_executable(${APP_NAME}
        src/widget/MainWindow.cpp
        src/widget/MainWindow.h
        src/widget/MainWindow.ui
        src/utils/qcustomplot.cpp
        src/utils/qcustomplot.h
        main.cpp
        src/widget/CsvAnalyzeWidget.cpp
        src/widget/CsvAnalyzeWidget.h
        src/widget/CsvAnalyzeWidget.ui
        src/widget/ExperimentRecordsWidget.cpp
        src/widget/ExperimentRecordsWidget.h
        src/widget/ExperimentRecordsWidget.ui
        src/database/DatabaseManager.cpp
        src/database/DatabaseManager.h
        src/widget/ImportExperimentDialog.cpp
        src/widget/ImportExperimentDialog.h
        src/widget/ImportExperimentDialog.ui
        src/resource/res.qrc
        src/utils/loadONNX.cpp
        src/utils/loadONNX.h
        src/widget/ResultVisualization.cpp
        src/widget/ResultVisualization.h
        src/widget/ResultVisualization.ui
)

add_subdirectory(src/include/QXlsx)

# ---------------- 链接依赖 ----------------
target_link_libraries(${APP_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::PrintSupport
        Qt6::Sql
        QXlsx::QXlsx
        ${OpenCV_LIBS}
        ONNXRuntime::ONNXRuntime
)

# Windows 下将依赖 DLL 复制到 exe 目录，OpenCV 自动匹配 Debug/Release。
if (WIN32)
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>" "$<TARGET_FILE_DIR:${APP_NAME}>"
            VERBATIM
    )
    foreach (opencv_target IN LISTS OpenCV_LIBS)
        get_target_property(opencv_target_type ${opencv_target} TYPE)
        if (opencv_target_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${APP_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${opencv_target}>" "$<TARGET_FILE_DIR:${APP_NAME}>"
                    VERBATIM
            )
        endif ()
    endforeach ()
endif ()

# ---------------- include 路径 ----------------
target_include_directories(${APP_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_BINARY_DIR}
)

# ---------------- 编译选项 ----------------
target_compile_options(${APP_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall -Wextra -Wpedantic
        >
)

# ---------------- 平台配置 ----------------
if (WIN32)
    target_compile_definitions(${APP_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00
    )

    # Debug有控制台，Release无控制台
    set_target_properties(${APP_NAME} PROPERTIES
            WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif ()

# ---------------- 构建信息注入 ----------------
target_compile_definitions(${APP_NAME} PRIVATE
        BUILD_GIT_HASH="${GIT_HASH}"
        BUILD_GIT_BRANCH="${GIT_BRANCH}"
        BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
        PROJECT_FULL_VERSION="${PROJECT_FULL_VERSION}"
)

# ---------------- 自动创建目录 ----------------
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(SUBDIRS
        "${SRC_DIR}/resource"
        "${SRC_DIR}/utils"
        "${SRC_DIR}/widget"
)

foreach (dir ${SUBDIRS})
    if (NOT EXISTS ${dir})
        file(MAKE_DIRECTORY ${dir})
    endif ()
endforeach ()
