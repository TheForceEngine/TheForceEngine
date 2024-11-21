if(WIN32)
	target_sources(tfe PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/filestream.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/fileutil.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/paths.cpp"
        )
elseif(UNIX)
	target_sources(tfe PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/filestream-posix.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/fileutil-posix.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/paths-posix.cpp"
	)
endif()
target_sources(tfe PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/filewriterAsync.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/memorystream.cpp"
		)

