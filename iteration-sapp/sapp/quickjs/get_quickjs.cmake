# Copyright (c) 2020 Kosaki Mezumona
# This cmake script is distributed under the MIT License.
# See LICENSE for more details.
cmake_minimum_required(VERSION 3.2)
set(quickjs_url https://bellard.org/quickjs/quickjs-2021-03-27.tar.xz)
set(quickjs_sha1 a33a7abb4471d566da39df90d0b688d120a8a2c4)
set(quickjs_extras_url https://bellard.org/quickjs/quickjs-extras-2021-03-27.tar.xz)
set(quickjs_extras_sha1 ae35cebc11f64e9c8bc97c53832607b0fe33953c)
set(quickjs_extract_dir ./sources)
set(quickjs_sources ./sources/quickjs-2021-03-27/quickjs.h;./sources/quickjs-2021-03-27/quickjs-libc.h;./sources/quickjs-2021-03-27/quickjs.c;./sources/quickjs-2021-03-27/libregexp.c;./sources/quickjs-2021-03-27/libunicode.c;./sources/quickjs-2021-03-27/libbf.c;./sources/quickjs-2021-03-27/cutils.c;./sources/quickjs-2021-03-27/quickjs-libc.c)

set(must_extract FALSE)
set(quickjs_tar /Users/andershammarnew/Documents/Lokalt/Iteration/iteration-sapp/sapp/quickjs/quickjs.tar.xz)
set(quickjs_extras_tar /Users/andershammarnew/Documents/Lokalt/Iteration/iteration-sapp/sapp/quickjs/quickjs-extra.tar.xz)

if(NOT EXISTS ${quickjs_tar})
	file(DOWNLOAD
		${quickjs_url}
		${quickjs_tar}
		EXPECTED_HASH SHA1=${quickjs_sha1}
	)
	set(must_extract TRUE)
endif()

if(NOT EXISTS ${quickjs_extras_tar})
	file(DOWNLOAD
		${quickjs_extras_url}
		${quickjs_extras_tar}
		EXPECTED_HASH SHA1=${quickjs_extras_sha1}
	)
	set(must_extract TRUE)
endif()

foreach(source IN LISTS quickjs_sources)
	if (NOT EXISTS ${source})
		set(must_extract TRUE)
		break()
	endif()
endforeach()


if (must_extract)
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${quickjs_extract_dir})
	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${quickjs_extract_dir})
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E tar xJ ${quickjs_tar}
		WORKING_DIRECTORY ${quickjs_extract_dir}
	)
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E tar xJ ${quickjs_extras_tar}
		WORKING_DIRECTORY ${quickjs_extract_dir}
	)
endif()
