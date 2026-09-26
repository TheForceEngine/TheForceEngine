file(GLOB_RECURSE SOURCES "*.cpp" "*.c")
if(LINUX)
	# we use the system libraries on Linux
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RtMidi.cpp")
endif()
if(NOT ENABLE_SYSMIDI)
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/systemMidiDevice.cpp")
	if(WIN32)
		list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RtMidi.cpp")
	endif()
endif()
if(NOT ENABLE_CLAP)
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/clapDevice.cpp")
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/clapPluginScanner.cpp")
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/clapModule.cpp")
endif()

target_sources(tfe PRIVATE ${SOURCES})

if(ENABLE_CLAP)
	target_compile_definitions(tfe PRIVATE BUILD_CLAP)
	# Header-only interface target provided by the FetchContent'd CLAP SDK (see root CMakeLists.txt).
	target_link_libraries(tfe PRIVATE clap)
	if(UNIX AND NOT APPLE)
		# Needed by clapModule.cpp (dlopen/dlsym/dlclose) to load .clap bundles at runtime.
		target_link_libraries(tfe PRIVATE ${CMAKE_DL_LIBS})
	endif()
endif()
