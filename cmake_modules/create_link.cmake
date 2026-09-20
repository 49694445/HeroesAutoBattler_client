
#message(${CMAKE_ARGV0}) # cmake.exe
#message(${CMAKE_ARGV1}) # -P
#message(${CMAKE_ARGV2}) # thisfilename
#message(${CMAKE_ARGV3}) # existing
#message(${CMAKE_ARGV4}) # linkname
set(source "${CMAKE_ARGV3}")
set(destination "${CMAKE_ARGV4}")
if(NOT IS_DIRECTORY "${source}")
	message(FATAL_ERROR "Asset link source is missing: ${source}")
endif()
# Live game processes can read these paths during a build. Never remove a
# working link, even briefly; refuse unexpected destinations instead.
if(EXISTS "${destination}" OR IS_SYMLINK "${destination}")
	file(REAL_PATH "${source}" source_real)
	file(REAL_PATH "${destination}" destination_real)
	if(WIN32)
		string(TOLOWER "${source_real}" source_real)
		string(TOLOWER "${destination_real}" destination_real)
	endif()
	if(source_real STREQUAL destination_real)
		return()
	endif()
	message(FATAL_ERROR "Refusing to replace existing asset path: ${destination}; expected ${source}")
endif()
if (WIN32)
	file(TO_NATIVE_PATH ${CMAKE_ARGV3} existing_native)
	file(TO_NATIVE_PATH ${CMAKE_ARGV4} linkname_native)
	execute_process(COMMAND cmd.exe /c mklink /J "${linkname_native}" "${existing_native}" RESULT_VARIABLE link_result)
else()
	execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${source}" "${destination}" RESULT_VARIABLE link_result)
endif()
if(NOT link_result EQUAL 0)
	message(FATAL_ERROR "Could not create asset link: ${destination}")
endif()
