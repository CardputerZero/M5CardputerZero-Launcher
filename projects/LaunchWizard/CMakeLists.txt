cmake_minimum_required(VERSION 3.20)

project(LaunchWizard VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(LAUNCH_WIZARD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(launch_wizard_core
    main/ui/manual_datetime_validation.cpp
    main/ui/wizard_model.cpp
)
add_library(LaunchWizard::core ALIAS launch_wizard_core)
set_target_properties(launch_wizard_core PROPERTIES EXPORT_NAME core)

target_compile_features(launch_wizard_core PUBLIC cxx_std_17)
target_include_directories(launch_wizard_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/main/ui>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(launch_wizard_core PRIVATE -Wall -Wextra -Wpedantic)
    if(LAUNCH_WIZARD_WARNINGS_AS_ERRORS)
        target_compile_options(launch_wizard_core PRIVATE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(launch_wizard_core PRIVATE /W4)
    if(LAUNCH_WIZARD_WARNINGS_AS_ERRORS)
        target_compile_options(launch_wizard_core PRIVATE /WX)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(launch_wizard_core_tests
        tests/test_manual_datetime_validation.cpp
        tests/test_wizard_model.cpp
    )
    target_link_libraries(launch_wizard_core_tests PRIVATE LaunchWizard::core)
    add_test(NAME launch_wizard.core
             COMMAND launch_wizard_core_tests)
endif()

install(TARGETS launch_wizard_core
    EXPORT LaunchWizardTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
    main/ui/manual_datetime_validation.h
    main/ui/wizard_model.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT LaunchWizardTargets
    FILE LaunchWizardTargets.cmake
    NAMESPACE LaunchWizard::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LaunchWizard
)

configure_package_config_file(
    cmake/LaunchWizardConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LaunchWizardConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LaunchWizard
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LaunchWizardConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LaunchWizardConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LaunchWizardConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LaunchWizard
)
