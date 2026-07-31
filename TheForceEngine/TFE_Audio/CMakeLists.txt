file(GLOB_RECURSE SOURCES "*.cpp" "*.c")
if(NOT ENABLE_SYSMIDI)
	list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/systemMidiDevice.cpp")
endif()

target_sources(tfe PRIVATE ${SOURCES})
