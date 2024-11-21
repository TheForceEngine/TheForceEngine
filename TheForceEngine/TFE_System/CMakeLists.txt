file(GLOB SOURCES "*.cpp" "*.c")
target_sources(tfe PRIVATE ${SOURCES})

if(WIN32)
	target_sources(tfe PRIVATE ${SRCWINTHRD}
			"${CMAKE_CURRENT_SOURCE_DIR}/CrashHandler/crashHandlerWin32.cpp"
	)
elseif(LINUX)
	target_sources(tfe PRIVATE ${SRCLNXTHRD}
		"${CMAKE_CURRENT_SOURCE_DIR}/CrashHandler/crashHandlerLinux.cpp"
	)
elseif(APPLE)
	target_sources(tfe PRIVATE ${SRCMACHTHRD}
		"${CMAKE_CURRENT_SOURCE_DIR}/CrashHandler/crashHandlerMac.cpp"
	)
endif()
