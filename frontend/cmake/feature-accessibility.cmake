target_sources(
  obs-studio
  PRIVATE
    accessibility/A11yAudit.cpp
    accessibility/A11yAudit.hpp
    accessibility/AccessibilityModule.cpp
    accessibility/AccessibilityModule.hpp
    accessibility/AccessibilitySettingsWidget.cpp
    accessibility/AccessibilitySettingsWidget.hpp
    accessibility/AccessibleBridge.cpp
    accessibility/AccessibleBridge.hpp
    accessibility/AccessibleCommon.cpp
    accessibility/AccessibleCommon.hpp
    accessibility/AccessibleFormat.cpp
    accessibility/AccessibleFormat.hpp
    accessibility/AccessibleLayout.cpp
    accessibility/AccessibleLayout.hpp
    accessibility/AccessibleMainWindow.cpp
    accessibility/AccessibleMainWindow.hpp
    accessibility/AccessibleSettings.cpp
    accessibility/AccessibleSettings.hpp
    accessibility/Announcer.cpp
    accessibility/Announcer.hpp
    accessibility/AudioActions.cpp
    accessibility/AudioActions.hpp
    accessibility/AudioPanel.cpp
    accessibility/AudioPanel.hpp
    accessibility/AudioPropertiesDialog.cpp
    accessibility/AudioPropertiesDialog.hpp
    accessibility/CommandPalette.cpp
    accessibility/CommandPalette.hpp
    accessibility/ControlsPanel.cpp
    accessibility/ControlsPanel.hpp
    accessibility/DialogFixer.cpp
    accessibility/DialogFixer.hpp
    accessibility/DocksDialog.cpp
    accessibility/DocksDialog.hpp
    accessibility/LayoutMode.cpp
    accessibility/LayoutMode.hpp
    accessibility/NvdaController.cpp
    accessibility/NvdaController.hpp
    accessibility/OutputAnnouncements.cpp
    accessibility/OutputAnnouncements.hpp
    accessibility/ScenesPanel.cpp
    accessibility/ScenesPanel.hpp
    accessibility/SourcesPanel.cpp
    accessibility/SourcesPanel.hpp
    accessibility/StatusMonitor.cpp
    accessibility/StatusMonitor.hpp
    accessibility/StatusPanel.cpp
    accessibility/StatusPanel.hpp
    accessibility/StatusReadouts.cpp
    accessibility/StatusReadouts.hpp
    accessibility/TransitionDialogs.cpp
    accessibility/TransitionDialogs.hpp
)

# NVDA speaks announcements made while MetaOBS is in the background only
# through its controller client, which NV Access publishes under the LGPL. It
# is downloaded once into .deps and shipped next to the program, with its
# license.
if(OS_WINDOWS)
  set(_nvda_controller_version "2026.1.1")
  set(_nvda_controller_hash "2a3a70f729be0c48120c5f24a81653202827c40a7986e289ffe2275a2b45011f")
  set(_nvda_controller_dir "${CMAKE_SOURCE_DIR}/.deps/nvda-controller-client-${_nvda_controller_version}")
  set(_nvda_controller_archive "${_nvda_controller_dir}/nvda_${_nvda_controller_version}_controllerClient.zip")

  if(NOT EXISTS "${_nvda_controller_dir}/x64/nvdaControllerClient.dll")
    message(STATUS "Downloading NVDA controller client ${_nvda_controller_version}")
    file(
      DOWNLOAD
      "https://download.nvaccess.org/releases/${_nvda_controller_version}/nvda_${_nvda_controller_version}_controllerClient.zip"
      "${_nvda_controller_archive}"
      EXPECTED_HASH SHA256=${_nvda_controller_hash}
      TLS_VERIFY ON
    )
    file(ARCHIVE_EXTRACT INPUT "${_nvda_controller_archive}" DESTINATION "${_nvda_controller_dir}")
    file(REMOVE "${_nvda_controller_archive}")
  endif()

  configure_file(
    "${_nvda_controller_dir}/license.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/nvdaControllerClient-license.txt"
    COPYONLY
  )

  target_add_resource(obs-studio "${_nvda_controller_dir}/x64/nvdaControllerClient.dll" "${OBS_EXECUTABLE_DESTINATION}")
  target_add_resource(
    obs-studio
    "${CMAKE_CURRENT_BINARY_DIR}/nvdaControllerClient-license.txt"
    "${OBS_EXECUTABLE_DESTINATION}"
  )
endif()
