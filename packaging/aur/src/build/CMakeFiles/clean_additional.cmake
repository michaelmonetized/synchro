# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "None")
  file(REMOVE_RECURSE
  "CMakeFiles/synchro_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_handler_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_handler_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_handlerplugin_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_handlerplugin_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_handlerplugin_init_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_handlerplugin_init_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_theme_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_theme_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_themeplugin_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_themeplugin_autogen.dir/ParseCache.txt"
  "CMakeFiles/synchro_themeplugin_init_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/synchro_themeplugin_init_autogen.dir/ParseCache.txt"
  "synchro_autogen"
  "synchro_handler_autogen"
  "synchro_handlerplugin_autogen"
  "synchro_handlerplugin_init_autogen"
  "synchro_theme_autogen"
  "synchro_themeplugin_autogen"
  "synchro_themeplugin_init_autogen"
  )
endif()
