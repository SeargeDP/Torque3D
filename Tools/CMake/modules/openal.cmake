# OpenAL module
option(TORQUE_SFX_OPENAL "Use OpenAL SFX" ON)

if(TORQUE_SFX_OPENAL)
  message("Enabling OpenAL Module")

  torqueAddSourceDirectories("${CMAKE_SOURCE_DIR}/Engine/source/sfx/openal")

  if (APPLE)
    torqueAddSourceDirectories("${CMAKE_SOURCE_DIR}/Engine/source/sfx/openal/mac")
  elseif (WIN32)
    torqueAddSourceDirectories("${CMAKE_SOURCE_DIR}/Engine/source/sfx/openal/win32")
  elseif (UNIX)
    torqueAddSourceDirectories("${CMAKE_SOURCE_DIR}/Engine/source/sfx/openal/linux")
  else()
    message(FATAL_ERROR "Unsupported OpenAL platform.")
  endif (APPLE)

  set(TORQUE_LINK_LIBRARIES ${TORQUE_LINK_LIBRARIES} OpenAL)

  # Since OpenAL lives elsewhere we need to ensure it is known to Torque when providing a link to it
  set(ALSOFT_EXAMPLES OFF CACHE BOOL "OpenAL Examples" FORCE)
  set(ALSOFT_UTILS OFF CACHE BOOL "OpenAL Utilities" FORCE)
  set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "Update build Version" UPDATE)
  
  add_subdirectory("${TORQUE_LIB_ROOT_DIRECTORY}/openal-soft" ${TORQUE_LIB_TARG_DIRECTORY}/openal-soft EXCLUDE_FROM_ALL)
endif(TORQUE_SFX_OPENAL)
