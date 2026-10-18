cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_A11Y_TESTS "Build accessibility tests" OFF)

if(NOT ENABLE_A11Y_TESTS)
  return()
endif()

if(NOT TARGET OBS::properties-view)
  message(WARNING "Accessibility tests require the frontend (ENABLE_FRONTEND) and are skipped")
  return()
endif()

find_package(Qt6 REQUIRED Core Widgets)

enable_testing()

add_executable(obs-a11y-tests)

target_sources(
  obs-a11y-tests
  PRIVATE
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/A11yAudit.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/A11yAudit.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/AccessibleFormat.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/AccessibleFormat.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/AccessibleLayout.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/AccessibleLayout.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/MidiMapping.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/MidiMapping.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/MidiProtocol.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/MidiProtocol.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckImages.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckImages.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckMapping.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckMapping.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckProtocol.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/accessibility/StreamDeckProtocol.hpp"
    "${CMAKE_SOURCE_DIR}/frontend/utility/MetaOBSRelease.cpp"
    "${CMAKE_SOURCE_DIR}/frontend/utility/MetaOBSRelease.hpp"
    a11y-test.hpp
    test-audit.cpp
    test-format.cpp
    test-layout.cpp
    test-main.cpp
    test-midi.cpp
    test-properties-view.cpp
    test-release.cpp
    test-streamdeck.cpp
)

target_include_directories(obs-a11y-tests PRIVATE "${CMAKE_SOURCE_DIR}/frontend")

target_link_libraries(obs-a11y-tests PRIVATE OBS::properties-view OBS::libobs OBS::frontend-api Qt::Widgets)

set_target_properties(
  obs-a11y-tests
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

# Installs the test next to obs64.exe in the run directory, where the OBS and Qt
# libraries it needs are bundled.
set_target_properties_obs(obs-a11y-tests PROPERTIES FOLDER "Tests and Examples")

# Each group name is passed to the executable, which runs the test cases whose
# names start with it.
foreach(test_group IN ITEMS Audit Format Layout Midi PropertiesView Release StreamDeck)
  add_test(
    NAME obs-a11y-${test_group}
    COMMAND "${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}/obs-a11y-tests" ${test_group}
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}"
  )

  set_tests_properties(
    obs-a11y-${test_group}
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}"
  )
endforeach()
