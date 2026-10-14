include_guard(DIRECTORY)

# MetaOBS has its own version, separate from the OBS version it is based on.
# Each release is tagged metaobs-v<version> on GitHub, and the built-in updater
# installs the newest one. build-aux/Publish-MetaOBS.ps1 reads both values.
set(METAOBS_VERSION "1.0.0")
set(METAOBS_REPOSITORY "lcasarotti/MetaOBS")

target_compile_definitions(
  obs-studio
  PRIVATE METAOBS_VERSION="${METAOBS_VERSION}" METAOBS_REPOSITORY="${METAOBS_REPOSITORY}"
)

target_sources(obs-studio PRIVATE utility/MetaOBSRelease.cpp utility/MetaOBSRelease.hpp)

if(OS_WINDOWS)
  target_sources(obs-studio PRIVATE utility/MetaOBSUpdater.cpp utility/MetaOBSUpdater.hpp)
endif()
