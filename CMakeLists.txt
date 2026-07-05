cmake_minimum_required(VERSION 3.20)

project(qhw_admission VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(QHW_ADM_BUILD_SHARED "Build shared qhw-admission library" ON)
option(QHW_ADM_BUILD_TESTS "Build qhw-admission tests" ON)
option(QHW_ADM_BUILD_PLUGINS "Build standard qhw-admission policy plugins" ON)
option(QHW_ADM_BUILD_PYTHON "Build Python bindings" ON)
option(QHW_ADM_BUILD_SWIG "Build SWIG Python extension" ON)
option(QHW_ADM_INSTALL_PLUGINS "Install standard policy plugins" ON)
option(QHW_ADM_INSTALL_PYTHON_ARTIFACTS
    "Install native artifacts into the Python package"
    OFF)

set(QHW_ADM_DATASTRUCTURES_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/external/qhw-datastructures
    CACHE PATH
    "Path to qhw-datastructures source tree")

if(NOT EXISTS ${QHW_ADM_DATASTRUCTURES_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
        "qhw-datastructures submodule is missing. Run "
        "'git submodule update --init --recursive'.")
endif()

if(NOT QHW_ADM_BUILD_SHARED)
    message(FATAL_ERROR
        "Phase 1 requires QHW_ADM_BUILD_SHARED=ON")
endif()

set(QHW_DS_BUILD_TESTS OFF CACHE BOOL
    "Disable qhw-datastructures tests from qhw-admission" FORCE)
set(QHW_DS_BUILD_BENCHMARKS OFF CACHE BOOL
    "Disable qhw-datastructures benchmarks from qhw-admission" FORCE)
add_subdirectory(
    ${QHW_ADM_DATASTRUCTURES_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/qhw-datastructures)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(QHW_ADM_BUILD_PYTHON AND NOT QHW_ADM_BUILD_SWIG)
    message(FATAL_ERROR
        "QHW_ADM_BUILD_PYTHON requires QHW_ADM_BUILD_SWIG=ON")
endif()

set(QHW_ADM_CORE_SOURCES
    src/qhw_admission.c
    src/qhw_config.c
    src/qhw_device.c
    src/qhw_error.c
    src/qhw_estimator.c
    src/qhw_math.c
    src/qhw_metadata.c
    src/qhw_policy.c
    src/qhw_registry.c
    src/qhw_reservation.c
    src/qhw_thread.c
    src/estimators/baseline.c)

add_library(qhw_admission SHARED ${QHW_ADM_CORE_SOURCES})
target_include_directories(qhw_admission
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qhw_admission PRIVATE qhw_datastructures)
if(UNIX)
    target_link_libraries(qhw_admission PRIVATE pthread ${CMAKE_DL_LIBS})
endif()
target_compile_options(qhw_admission PRIVATE
    -Wall
    -Wextra
    -Werror)
set_target_properties(qhw_admission PROPERTIES
    OUTPUT_NAME qhw_admission
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    POSITION_INDEPENDENT_CODE TRUE
    BUILD_RPATH "$ORIGIN/qhw-datastructures:$ORIGIN"
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN")

set(QHW_ADM_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_admission/qhw_admission.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_admission/qhw_admission_abi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qhw_admission/qhw_admission_types.h)

if(QHW_ADM_BUILD_PLUGINS)
    add_library(qhw_adm_unlimited MODULE src/policies/unlimited.c)
    target_include_directories(qhw_adm_unlimited
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_unlimited PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_unlimited PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/policies)
endif()

if(QHW_ADM_BUILD_TESTS)
    enable_testing()

    add_executable(test_context tests/c/test_context.c)
    target_include_directories(test_context
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_context PRIVATE
        qhw_admission
        qhw_datastructures)
    add_test(NAME test_context COMMAND test_context)

    add_executable(test_estimator tests/c/test_estimator.c)
    target_include_directories(test_estimator
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_estimator PRIVATE
        qhw_admission
        qhw_datastructures)
    target_compile_definitions(test_estimator PRIVATE
        "QHW_ADM_TEST_PLUGIN_PATH=\"$<TARGET_FILE:qhw_adm_estimator_mock>\""
        "QHW_ADM_TEST_PLUGIN_DIR=\"$<TARGET_FILE_DIR:qhw_adm_estimator_mock>\"")
    add_test(NAME test_estimator COMMAND test_estimator)

    add_executable(test_policy tests/c/test_policy.c)
    target_include_directories(test_policy
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_policy PRIVATE
        qhw_admission
        qhw_datastructures)
    target_compile_definitions(test_policy PRIVATE
        "QHW_ADM_TEST_POLICY_PATH=\"$<TARGET_FILE:qhw_adm_mock>\""
        "QHW_ADM_TEST_POLICY_DIR=\"$<TARGET_FILE_DIR:qhw_adm_mock>\""
        "QHW_ADM_TEST_PLUGIN_DIR=\"$<TARGET_FILE_DIR:qhw_adm_estimator_mock>\""
        "QHW_ADM_TEST_BAD_CAPS_PATH=\"$<TARGET_FILE:qhw_adm_bad_caps>\""
        "QHW_ADM_TEST_BAD_GRANT_DIR=\"$<TARGET_FILE_DIR:qhw_adm_bad_grant>\""
        "QHW_ADM_TEST_ERROR_OUTPUT_DIR=\"$<TARGET_FILE_DIR:qhw_adm_error_output>\"")
    add_test(NAME test_policy COMMAND test_policy)

    if(QHW_ADM_BUILD_PLUGINS)
        add_executable(test_unlimited tests/c/test_unlimited.c)
        target_include_directories(test_unlimited
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(test_unlimited PRIVATE
            qhw_admission
            qhw_datastructures)
        target_compile_definitions(test_unlimited PRIVATE
            "QHW_ADM_TEST_UNLIMITED_DIR=\"$<TARGET_FILE_DIR:qhw_adm_unlimited>\"")
        add_dependencies(test_unlimited qhw_adm_unlimited)
        add_test(NAME test_unlimited COMMAND test_unlimited)
    endif()

    add_library(qhw_adm_estimator_mock MODULE
        tests/plugins/qhw_adm_estimator_mock.c)
    target_include_directories(qhw_adm_estimator_mock
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_estimator_mock PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_estimator_mock PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/test-plugins)
    add_dependencies(test_estimator qhw_adm_estimator_mock)

    add_library(qhw_adm_mock MODULE tests/plugins/qhw_adm_mock.c)
    target_include_directories(qhw_adm_mock
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_mock PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_mock PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/test-plugins)
    add_library(qhw_adm_bad_caps MODULE tests/plugins/qhw_adm_bad_caps.c)
    target_include_directories(qhw_adm_bad_caps
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_bad_caps PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_bad_caps PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/test-plugins)

    add_library(qhw_adm_bad_grant MODULE tests/plugins/qhw_adm_bad_grant.c)
    target_include_directories(qhw_adm_bad_grant
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_bad_grant PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_bad_grant PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/test-plugins)
    add_library(qhw_adm_error_output MODULE
        tests/plugins/qhw_adm_error_output.c)
    target_include_directories(qhw_adm_error_output
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(qhw_adm_error_output PRIVATE
        -Wall
        -Wextra
        -Werror)
    set_target_properties(qhw_adm_error_output PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/test-plugins)
    add_dependencies(test_policy
        qhw_adm_mock
        qhw_adm_bad_caps
        qhw_adm_bad_grant
        qhw_adm_error_output)
endif()

install(
    TARGETS qhw_admission
    EXPORT qhw_admissionTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
    DIRECTORY include/qhw_admission
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(QHW_ADM_BUILD_PLUGINS AND QHW_ADM_INSTALL_PLUGINS)
    install(
        TARGETS qhw_adm_unlimited
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qhw_admission/policies)
endif()

install(
    EXPORT qhw_admissionTargets
    NAMESPACE qhw_admission::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qhw_admission)

if(QHW_ADM_INSTALL_PYTHON_ARTIFACTS)
    install(
        TARGETS qhw_admission
        LIBRARY DESTINATION qhw_admission/native
        ARCHIVE DESTINATION qhw_admission/native
        RUNTIME DESTINATION qhw_admission/native)
    install(
        TARGETS qhw_datastructures
        LIBRARY DESTINATION qhw_admission/native
        ARCHIVE DESTINATION qhw_admission/native
        RUNTIME DESTINATION qhw_admission/native)
endif()

if(QHW_ADM_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(SWIG REQUIRED)
    find_package(Python3 COMPONENTS Development.Module REQUIRED)

    set(QHW_ADM_PYTHON_BUILD_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/python/qhw_admission)
    add_custom_target(qhw_admission_python_tree ALL
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/python/qhw_admission
            ${QHW_ADM_PYTHON_BUILD_DIR}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${QHW_ADM_PYTHON_BUILD_DIR}/native
        COMMAND
            ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:qhw_admission>
            ${QHW_ADM_PYTHON_BUILD_DIR}/native
        COMMAND
            ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:qhw_datastructures>
            ${QHW_ADM_PYTHON_BUILD_DIR}/native
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE_NAME:qhw_datastructures>
            ${QHW_ADM_PYTHON_BUILD_DIR}/native/$<TARGET_SONAME_FILE_NAME:qhw_datastructures>
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_SONAME_FILE_NAME:qhw_datastructures>
            ${QHW_ADM_PYTHON_BUILD_DIR}/native/$<TARGET_LINKER_FILE_NAME:qhw_datastructures>
        DEPENDS
            qhw_admission
            qhw_datastructures)

    set(QHW_ADM_SWIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/swig)
    set(QHW_ADM_SWIG_C ${QHW_ADM_SWIG_DIR}/qhw_admission_wrap.c)
    set(QHW_ADM_SWIG_PY ${QHW_ADM_PYTHON_BUILD_DIR}/_qhw_admission.py)

    add_custom_command(
        OUTPUT ${QHW_ADM_SWIG_C} ${QHW_ADM_SWIG_PY}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${QHW_ADM_SWIG_DIR}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${QHW_ADM_PYTHON_BUILD_DIR}
        COMMAND
            ${SWIG_EXECUTABLE}
            -python
            -module _qhw_admission
            -interface _qhw_admission_c
            -outdir ${QHW_ADM_PYTHON_BUILD_DIR}
            -o ${QHW_ADM_SWIG_C}
            -I${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/swg/qhw_admission.i
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/swg/qhw_admission.i
            ${QHW_ADM_PUBLIC_HEADERS}
        VERBATIM)

    add_library(_qhw_admission_c MODULE ${QHW_ADM_SWIG_C})
    target_include_directories(_qhw_admission_c
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${Python3_INCLUDE_DIRS})
    target_link_libraries(_qhw_admission_c
        PRIVATE
            qhw_admission
            Python3::Module)
    set_target_properties(_qhw_admission_c PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${QHW_ADM_PYTHON_BUILD_DIR}
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/native:$ORIGIN")
    add_dependencies(_qhw_admission_c qhw_admission_python_tree)

    if(QHW_ADM_INSTALL_PYTHON_ARTIFACTS)
        install(
            TARGETS _qhw_admission_c
            LIBRARY DESTINATION qhw_admission)
        install(
            FILES ${QHW_ADM_SWIG_PY}
            DESTINATION qhw_admission)
    endif()
endif()

if(QHW_ADM_BUILD_PYTHON AND QHW_ADM_BUILD_TESTS)
    add_test(
        NAME test_python_context
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_context.py)
    add_test(
        NAME test_python_estimator
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_estimator.py)
    add_test(
        NAME test_python_policy
        COMMAND
            ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
            QHW_ADM_TEST_POLICY_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-plugins
            ${Python3_EXECUTABLE}
            -S
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_policy.py)
    if(QHW_ADM_BUILD_PLUGINS)
        add_test(
            NAME test_python_unlimited
            COMMAND
                ${CMAKE_COMMAND} -E env
                PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python
                QHW_ADM_TEST_UNLIMITED_DIR=${CMAKE_CURRENT_BINARY_DIR}/policies
                ${Python3_EXECUTABLE}
                -S
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_unlimited.py)
    endif()
endif()
