# Write explicit Qt resource entries instead of copying/symlinking entire
# source directories. Aliases retain the :/config and :/Mods runtime layout.
# CONFIGURE_DEPENDS makes additions/removals regenerate the manifest.
function(vcmi_generate_mobile_resource resourceName resourceDir qrcFile)
	file(GLOB_RECURSE resourceFiles LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
		RELATIVE "${resourceDir}" "${resourceDir}/*")
	list(SORT resourceFiles)
	set(resourceXml "<RCC>\n<qresource prefix=\"/${resourceName}\">\n")
	foreach(resourceFile IN LISTS resourceFiles)
		if(VCMI_REMOTE_CLIENT_ONLY)
			string(TOLOWER "${resourceName}/${resourceFile}" resourceLower)
			if(resourceLower MATCHES "^config/ai/")
				continue()
			endif()
		endif()
		set(resourcePath "${resourceDir}/${resourceFile}")
		# Paths may contain XML-special characters (including in the workspace).
		foreach(xmlVariable resourceFile resourcePath)
			string(REPLACE "&" "&amp;" ${xmlVariable} "${${xmlVariable}}")
			string(REPLACE "<" "&lt;" ${xmlVariable} "${${xmlVariable}}")
			string(REPLACE ">" "&gt;" ${xmlVariable} "${${xmlVariable}}")
			string(REPLACE "\"" "&quot;" ${xmlVariable} "${${xmlVariable}}")
		endforeach()
		string(APPEND resourceXml "<file alias=\"${resourceFile}\">${resourcePath}</file>\n")
	endforeach()
	string(APPEND resourceXml "</qresource>\n</RCC>\n")
	# Preserve the timestamp when reconfiguring without resource changes.
	# Otherwise AUTORCC rebuilds even the large MMAI model resources each time.
	if(EXISTS "${qrcFile}")
		file(READ "${qrcFile}" previousResourceXml)
		if("${previousResourceXml}" STREQUAL "${resourceXml}")
			return()
		endif()
	endif()
	file(WRITE "${qrcFile}" "${resourceXml}")
endfunction()
