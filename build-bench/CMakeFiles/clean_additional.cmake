# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/NAMPlayerBench_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/NAMPlayerBench_autogen.dir/ParseCache.txt"
  "CMakeFiles/NAMPlayer_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/NAMPlayer_autogen.dir/ParseCache.txt"
  "NAMPlayerBench_autogen"
  "NAMPlayer_autogen"
  )
endif()
