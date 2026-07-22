# CMake Windows helper functions module

include_guard(GLOBAL)

include(helpers_common)

# set_target_properties_plugin: Set target properties for use in obs-studio
function(set_target_properties_plugin target)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 0 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  message(DEBUG "Setting additional properties for target ${target}...")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  string(TIMESTAMP CURRENT_YEAR "%Y")

  set_target_properties(${target} PROPERTIES VERSION 0 SOVERSION ${PLUGIN_VERSION})

  install(TARGETS ${target} RUNTIME DESTINATION "${target}/bin/64bit" LIBRARY DESTINATION "${target}/bin/64bit")

  set(_kifume_runtime_hint_dirs)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _kifume_runtime_hint_dirs
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
    )
  endif()

  foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    if(EXISTS "${_prefix}/bin")
      list(APPEND _kifume_runtime_hint_dirs "${_prefix}/bin")
    endif()
    if(EXISTS "${_prefix}/debug/bin")
      list(APPEND _kifume_runtime_hint_dirs "${_prefix}/debug/bin")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _kifume_runtime_hint_dirs)

  set(_kifume_runtime_candidates
    z.dll
    zd.dll
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    libprotobufd.dll
    re2d.dll
    caresd.dll
  )

  set(_kifume_extra_runtime_dlls)
  foreach(_dll_name IN LISTS _kifume_runtime_candidates)
    foreach(_dll_dir IN LISTS _kifume_runtime_hint_dirs)
      if(EXISTS "${_dll_dir}/${_dll_name}")
        list(APPEND _kifume_extra_runtime_dlls "${_dll_dir}/${_dll_name}")
        break()
      endif()
    endforeach()
  endforeach()

  # Bundle transitive runtime DLLs (for example, gRPC/protobuf from vcpkg)
  # so OBS can load the plugin outside a developer shell.
  install(
    FILES $<TARGET_RUNTIME_DLLS:${target}>
    DESTINATION "${target}/bin/64bit"
    CONFIGURATIONS RelWithDebInfo Debug Release
    OPTIONAL
  )

  if(_kifume_extra_runtime_dlls)
    install(
      FILES ${_kifume_extra_runtime_dlls}
      DESTINATION "${target}/bin/64bit"
      CONFIGURATIONS RelWithDebInfo Debug Release
      OPTIONAL
    )
  endif()

  install(
    FILES "$<TARGET_PDB_FILE:${target}>"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "${target}/bin/64bit"
    OPTIONAL
  )

  if(TARGET plugin-support)
    target_link_libraries(${target} PRIVATE plugin-support)
  endif()

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>"
      "$<$<CONFIG:Debug,RelWithDebInfo,Release>:$<TARGET_PDB_FILE:${target}>>"
      "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      $<TARGET_RUNTIME_DLLS:${target}>
      "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMENT "Copy ${target} to rundir"
    COMMAND_EXPAND_LISTS
    VERBATIM
  )

  if(_kifume_extra_runtime_dlls)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        ${_kifume_extra_runtime_dlls}
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
      COMMENT "Copy ${target} extra runtime DLLs to rundir"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()

  target_install_resources(${target})

  get_target_property(target_sources ${target} SOURCES)
  set(target_ui_files ${target_sources})
  list(FILTER target_ui_files INCLUDE REGEX ".+\\.(ui|qrc)")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "UI Files" FILES ${target_ui_files})

  configure_file(cmake/windows/resources/resource.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")
endfunction()

# Helper function to add resources into bundle
function(target_install_resources target)
  message(DEBUG "Installing resources for target ${target}...")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    file(GLOB_RECURSE data_files "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    foreach(data_file IN LISTS data_files)
      cmake_path(
        RELATIVE_PATH
        data_file
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        OUTPUT_VARIABLE relative_path
      )
      cmake_path(GET relative_path PARENT_PATH relative_path)
      target_sources(${target} PRIVATE "${data_file}")
      source_group("Resources/${relative_path}" FILES "${data_file}")
    endforeach()

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "${target}/data" USE_SOURCE_PERMISSIONS)

    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
      COMMENT "Copy ${target} resources to rundir"
      VERBATIM
    )
  endif()
endfunction()

# Helper function to add a specific resource to a bundle
function(target_add_resource target resource)
  message(DEBUG "Add resource '${resource}' to target ${target} at destination '${target_destination}'...")

  install(FILES "${resource}" DESTINATION "${target}/data" COMPONENT Runtime)

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${resource}" "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMENT "Copy ${target} resource ${resource} to rundir"
    VERBATIM
  )
  source_group("Resources" FILES "${resource}")
endfunction()
