# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# mod_geist: an Apache 2.4 module that browses BOO books over HTTP.
#
# It links libgeist statically, so an operator deploys one .so and no shared
# library alongside it. Its CSS, JavaScript and icons are likewise compiled
# in, by tools/bundle.py, so there is nothing to install beside the module.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GEIST_ASSET_STAGE "${CMAKE_CURRENT_BINARY_DIR}/assets")
set(GEIST_ASSET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/assets.hpp")

file(GLOB_RECURSE GEIST_ASSET_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
)

# Depending on the asset files alone catches an edit or an addition, but not
# a removal: the deleted file just drops out of the dependency list and the
# stale bundle looks up to date. Depend on a manifest of the list instead,
# rewritten only when the set actually changes.
set(GEIST_ASSET_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/asset-manifest.txt")
string(REPLACE ";" "\n" GEIST_ASSET_MANIFEST_BODY "${GEIST_ASSET_FILES}")
file(WRITE "${GEIST_ASSET_MANIFEST}.in" "${GEIST_ASSET_MANIFEST_BODY}\n")
configure_file("${GEIST_ASSET_MANIFEST}.in" "${GEIST_ASSET_MANIFEST}" COPYONLY)

# The published stylesheet is bundled as book.css so a topic served here is
# styled exactly as the specification says it should be.
add_custom_command(
  OUTPUT "${GEIST_ASSET_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${GEIST_ASSET_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${GEIST_ASSET_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/doc/html-styling.css"
          "${GEIST_ASSET_STAGE}/book.css"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle.py"
          "${GEIST_ASSET_HEADER}" "${GEIST_ASSET_STAGE}"
  DEPENDS ${GEIST_ASSET_FILES}
          "${GEIST_ASSET_MANIFEST}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle.py"
          "${CMAKE_SOURCE_DIR}/doc/html-styling.css"
  COMMENT "Embedding mod_geist assets"
  VERBATIM
)

add_library(mod_geist MODULE mod_geist.cpp "${GEIST_ASSET_HEADER}")

set_target_properties(mod_geist PROPERTIES
  PREFIX ""            # Apache loads mod_geist.so, not libmod_geist.so.
  OUTPUT_NAME "mod_geist"
  SUFFIX ".so"
)

target_compile_features(mod_geist PRIVATE cxx_std_17)
target_include_directories(mod_geist PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  ${GEIST_APACHE_INCLUDE_DIRS}
)
# Which libgeist the module links.
#
# By default the static one, so the shipped module is a single .so with
# nothing beside it -- what a hand-installed module wants. A distribution
# package wants the opposite: linking the shared library means one copy to
# patch when libgeist is fixed, and the package manager records the
# dependency. Debian packaging turns this on.
option(GEIST_APACHE_SHARED_LIBGEIST
  "Link mod_geist against the shared libgeist rather than the static one" OFF
)
if(GEIST_APACHE_SHARED_LIBGEIST)
  target_link_libraries(mod_geist PRIVATE geist::geist)
else()
  target_link_libraries(mod_geist PRIVATE geist::geist_static)
endif()

if(GEIST_APACHE_LIBRARIES)
  target_link_libraries(mod_geist PRIVATE ${GEIST_APACHE_LIBRARIES})
endif()

target_compile_options(mod_geist PRIVATE -Wall -Wextra)

# httpd resolves the module's symbols against itself at load time.
if(APPLE)
  target_link_options(mod_geist PRIVATE -undefined dynamic_lookup)
endif()

install(TARGETS mod_geist LIBRARY DESTINATION "${GEIST_APACHE_LIBEXECDIR}")

# Installation --------------------------------------------------------------
#
# The module goes where httpd loads modules from, and its configuration where
# httpd reads configuration -- both outside CMAKE_INSTALL_PREFIX, because
# httpd owns those locations. DESTDIR is honoured throughout, so packaging
# still works.

option(GEIST_APACHE_ENABLE
  "Enable the module in httpd's configuration at install time" ON
)

configure_file(geist.load.in "${CMAKE_CURRENT_BINARY_DIR}/geist.load" @ONLY)
configure_file(geist.conf.in "${CMAKE_CURRENT_BINARY_DIR}/geist.conf" @ONLY)

install(TARGETS mod_geist LIBRARY DESTINATION "${GEIST_APACHE_LIBEXECDIR}")

# Which configuration layout httpd on this system uses.
#
# AUTO decides by looking for mods-available, which is right for a build that
# installs onto the machine it ran on. A package build is not that: it runs in
# a container that has apache2-dev but no httpd configuration, so AUTO would
# read the absence of mods-available as "this is Red Hat" and file the module
# where no Debian package expects it. Packaging says which layout it wants.
set(GEIST_APACHE_CONFIG_LAYOUT "AUTO" CACHE STRING
  "httpd configuration layout: AUTO, DEBIAN or REDHAT"
)
set_property(CACHE GEIST_APACHE_CONFIG_LAYOUT
  PROPERTY STRINGS AUTO DEBIAN REDHAT
)

if(GEIST_APACHE_CONFIG_LAYOUT STREQUAL "DEBIAN")
  set(_geist_apache_debian_layout TRUE)
elseif(GEIST_APACHE_CONFIG_LAYOUT STREQUAL "REDHAT")
  set(_geist_apache_debian_layout FALSE)
elseif(GEIST_APACHE_CONFIG_LAYOUT STREQUAL "AUTO")
  if(EXISTS "${GEIST_APACHE_SYSCONFDIR}/mods-available")
    set(_geist_apache_debian_layout TRUE)
  else()
    set(_geist_apache_debian_layout FALSE)
  endif()
else()
  message(FATAL_ERROR
    "GEIST_APACHE_CONFIG_LAYOUT must be AUTO, DEBIAN or REDHAT, not "
    "'${GEIST_APACHE_CONFIG_LAYOUT}'."
  )
endif()

if(_geist_apache_debian_layout)
  # Debian and Ubuntu: definitions live in mods-available and are enabled by
  # a symlink from mods-enabled, which is what a2enmod writes.
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/geist.load"
    "${CMAKE_CURRENT_BINARY_DIR}/geist.conf"
    DESTINATION "${GEIST_APACHE_SYSCONFDIR}/mods-available"
  )

  if(GEIST_APACHE_ENABLE)
    install(CODE "
      set(_enabled \"\$ENV{DESTDIR}${GEIST_APACHE_SYSCONFDIR}/mods-enabled\")
      file(MAKE_DIRECTORY \"\${_enabled}\")
      foreach(_file geist.load geist.conf)
        file(CREATE_LINK
          \"../mods-available/\${_file}\" \"\${_enabled}/\${_file}\"
          SYMBOLIC
        )
        message(STATUS \"Enabled \${_enabled}/\${_file}\")
      endforeach()
      message(STATUS \"mod_geist enabled; restart httpd to load it\")
    ")
  endif()
else()
  # Red Hat and friends: a single drop-in that httpd includes on its own, so
  # there is nothing to enable separately.
  install(CODE "
    file(READ \"${CMAKE_CURRENT_BINARY_DIR}/geist.load\" _load)
    file(READ \"${CMAKE_CURRENT_BINARY_DIR}/geist.conf\" _conf)
    file(WRITE \"\$ENV{DESTDIR}${GEIST_APACHE_SYSCONFDIR}/conf.modules.d/10-geist.conf\"
      \"\${_load}\${_conf}\")
    message(STATUS \"Installed 10-geist.conf; restart httpd to load it\")
  ")
endif()
