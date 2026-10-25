include(FetchContent)

# CLAP instrument plugin support as a music synthesis backend (alternative to SF2
# soundfonts, OPL3 emulation, and System Midi). ENABLE_CLAP is declared once, at the
# top-level CMakeLists.txt, right next to ENABLE_SYSMIDI - not redeclared here.
if(ENABLE_CLAP)
	# Pull in the official, header-only CLAP SDK (https://github.com/free-audio/clap).
	# This only provides the plugin ABI headers - plugins themselves are loaded
	# dynamically at runtime (see TFE_Audio/clapModule.cpp), so no linking is
	# required beyond the interface include directories.
	FetchContent_Declare(
		clap
		GIT_REPOSITORY https://github.com/free-audio/clap.git
		GIT_TAG        1.2.6
	)
	FetchContent_MakeAvailable(clap)
endif()

add_subdirectory(TFE_A11y/)
add_subdirectory(TFE_Archive/)
add_subdirectory(TFE_Asset/)
add_subdirectory(TFE_Audio/)
add_subdirectory(TFE_DarkForces/)
add_subdirectory(TFE_ExternalData/)
add_subdirectory(TFE_FileSystem/)
add_subdirectory(TFE_FrontEndUI/)
add_subdirectory(TFE_Game/)
add_subdirectory(TFE_Input/)
add_subdirectory(TFE_Jedi/)
add_subdirectory(TFE_Memory/)
add_subdirectory(TFE_Outlaws/)
add_subdirectory(TFE_Polygon/)
add_subdirectory(TFE_PostProcess/)
add_subdirectory(TFE_RenderShared/)
add_subdirectory(TFE_RenderBackend/)
add_subdirectory(TFE_Settings/)
add_subdirectory(TFE_System/)
add_subdirectory(TFE_Ui/)
if(ENABLE_EDITOR)
	add_subdirectory(TFE_Editor/)
endif()
add_subdirectory(TFE_ForceScript/)
target_sources(tfe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
