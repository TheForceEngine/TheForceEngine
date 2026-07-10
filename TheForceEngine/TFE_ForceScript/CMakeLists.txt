file(GLOB_RECURSE SOURCES "*.cpp" "*.c")

set(ARCHFLAGS "${CMAKE_SYSTEM_PROCESSOR}")

if (APPLE AND NOT IOS)
	# TODO: Would this break building on armv7 Linux?
	if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
		set(ARCHFLAGS "aarch64")
	endif ()
endif ()

if(${ARCHFLAGS} MATCHES "^arm")
	if(CMAKE_ASM_COMPILER_WORKS)
		if(WIN32)
			list(APPEND SOURCES
			     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm_msvc.asm)
		else()
			list(APPEND SOURCES
			     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm_gcc.S)
			set_property(SOURCE
				     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm_gcc.S
				     APPEND PROPERTY COMPILE_FLAGS " -Wa,-mimplicit-it=always")
		endif()
	   else()
		message(FATAL ERROR "ARM target requires a working assembler")
	endif()
endif()

if(${ARCHFLAGS} MATCHES "^aarch64")
	if(CMAKE_ASM_COMPILER_WORKS)
		if(WIN32)
			list(APPEND SOURCES
			     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm64_msvc.asm)
		elseif(NOT APPLE)
			list(APPEND SOURCES
			     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm64_gcc.S)
		else()
			list(APPEND SOURCES
			     TheForceEngine/TFE_ForceScript/Angelscript/angelscript/source/as_callfunc_arm64_xcode.S)
		endif()
	else()
		message(FATAL ERROR "ARM target requires a working assembler")
	endif()
endif()

target_sources(tfe PRIVATE ${SOURCES})
