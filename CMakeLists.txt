cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
include(CheckCXXCompilerFlag)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

# force Release by default.
if(NOT DEFINED CMAKE_BUILD_TYPE)
       set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

project(TheForceEngine
	HOMEPAGE_URL "https://theforceengine.github.io"
	DESCRIPTION "Modern 'Jedi Engine' replacement supporting Dark Forces, mods, and in the future Outlaws."
	LANGUAGES C CXX ASM
	)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

## gcc-12+ and clang-15+ have a feature to automatically zero all variables/members/...
## this mimics what modern MSVC does.  Enable it for release builds (i.e.
## when not debugging to not hide any real bugs).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	check_cxx_compiler_flag("-ftrivial-auto-var-init=zero" COMPILER_ENABLE_AUTOZERO)
endif()

# disable Clangs excessive warnings about unhandled switch values
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch")
endif()

if (UNIX AND NOT APPLE)
	set(LINUX ON)
endif()

if(WIN32)
	# windows: drop everything into one folder
        set(CMAKE_INSTALL_BINDIR ".")
        set(CMAKE_INSTALL_LIBDIR ".")
        set(CMAKE_INSTALL_DATADIR ".")
else()
	set(TFE_ICONDIR "share/icons/hicolor")
	# tweak DATADIR to end up with ./share/TheForceEngine/
        set(CMAKE_INSTALL_DATADIR "share/${PROJECT_NAME}"
                CACHE PATH "Read-only architecture-independent data"
        )
endif()
include(GNUInstallDirs)

## Options
option(ENABLE_TFE "Enable building “The Force Engine”" ON)
option(ENABLE_SYSMIDI "Enable System-MIDI Output if RTMidi is available" ON)
option(ENABLE_EDITOR "Enable TFE Editor" OFF)
option(ENABLE_ADJUSTABLEHUD_MOD "Install the build‑in “AdjustableHud mod” with TFE" ON)

if(ENABLE_TFE)
	add_executable(tfe)

	if(APPLE)
		# cant have executable have the same name as the cmake working dir
		set_target_properties(tfe PROPERTIES OUTPUT_NAME "theforceengine_osx")
	else()
		set_target_properties(tfe PROPERTIES OUTPUT_NAME "theforceengine")
	endif()

	if(UNIX)
		set(THREADS_PREFER_PTHREAD_FLAG ON)
		find_package(Threads REQUIRED)
		find_package(PkgConfig REQUIRED)

		pkg_check_modules(SDL2 REQUIRED sdl2)
		pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

		target_include_directories(tfe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

		target_include_directories(tfe PRIVATE ${SDL2_INCLUDE_DIRS})
		target_include_directories(tfe PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
		target_link_libraries(tfe PRIVATE ${SDL2_LIBRARIES}
					${SDL2_IMAGE_LIBRARIES}
					${CMAKE_DL_LIBS}
					Threads::Threads
		)
		
		target_link_directories(tfe PRIVATE ${SDL2_LIBRARY_DIRS} ${SDL2_IMAGE_LIBRARY_DIRS})		

		# set up build directory to be able to run TFE immediately: symlink
		# the necessary support file directories into the build env.
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Captions)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Documentation)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/ExternalData)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Fonts)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Mods)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Shaders)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/SoundFonts)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/UI_Images)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/UI_Text)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/EditorDef)
		execute_process(COMMAND ln -sf ${CMAKE_SOURCE_DIR}/TheForceEngine/Tests)
		include(CreateGitVersionH.cmake)
		create_git_version_h()
	endif()

	if(COMPILER_ENABLE_AUTOZERO)
		message(STATUS "enabled -ftrivial-auto-var-init=zero")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftrivial-auto-var-init=zero")
	endif()

	if(ENABLE_SYSMIDI)
		pkg_check_modules(RTMIDI rtmidi>=5.0.0)
		if(RTMIDI_FOUND)
			add_definitions("-DBUILD_SYSMIDI")
			target_link_libraries(tfe PRIVATE ${RTMIDI_LIBRARIES})
		else()
			set(ENABLE_SYSMIDI 0)
			message(STATUS "System MIDI Disabled")
		endif()
	endif()
	if(ENABLE_EDITOR)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_EDITOR")
	endif()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_FORCE_SCRIPT")


	target_include_directories(tfe PRIVATE "TheForceEngine/TFE_ForceScript/Angelscript/angelscript/include")
	target_include_directories(tfe PRIVATE "TheForceEngine/TFE_ForceScript/Angelscript/add_on")
	target_include_directories(tfe PRIVATE TheForceEngine)

	add_subdirectory(TheForceEngine/)
endif()

### installation ###

if(ENABLE_TFE)
	# Main binary
	install(TARGETS tfe
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
		BUNDLE  DESTINATION "${CMAKE_INSTALL_BINDIR}"
	)

	# Support data
	install(DIRECTORY
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/Captions"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/Documentation"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/ExternalData"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/UI_Text"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/UI_Images"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/EditorDef"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/Shaders"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/SoundFonts"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/Fonts"
		"${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/Tests"
		DESTINATION "${CMAKE_INSTALL_DATADIR}"
		    FILE_PERMISSIONS
		      OWNER_READ OWNER_WRITE
		      GROUP_READ
		      WORLD_READ
		    DIRECTORY_PERMISSIONS
		      OWNER_READ OWNER_EXECUTE OWNER_WRITE
		      GROUP_READ GROUP_EXECUTE GROUP_WRITE
		      WORLD_READ WORLD_EXECUTE
	)
	# Always install the “Mods” directory but not always the “AdjustableHud” mod
	install(DIRECTORY
		DESTINATION "${CMAKE_INSTALL_DATADIR}/TheForceEngine/Mods"
		DIRECTORY_PERMISSIONS
			OWNER_READ OWNER_EXECUTE OWNER_WRITE
			GROUP_READ GROUP_EXECUTE GROUP_WRITE
			WORLD_READ WORLD_EXECUTE
	)

	# Linux .desktop files
	if(LINUX)
		install(
			FILES "${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/io.github.theforceengine.tfe.desktop" DESTINATION "share/applications"
		)
		install(
			FILES "${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/TheForceEngine.svg" DESTINATION "${TFE_ICONDIR}/scalable/apps"
   			RENAME "io.github.theforceengine.tfe.svg"
		)
		install(
			FILES "${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/io.github.theforceengine.tfe.metainfo.xml"
			DESTINATION "share/metainfo"
		)
	endif()
endif()

if(ENABLE_ADJUSTABLEHUD_MOD)
	add_subdirectory(TheForceEngine/Mods/TFE/AdjustableHud)

	if(LINUX)
		install(
			FILES "${CMAKE_CURRENT_SOURCE_DIR}/TheForceEngine/io.github.theforceengine.tfe.Mod.AdjustableHud.metainfo.xml"
			DESTINATION "share/metainfo"
		)
	endif()
endif()
