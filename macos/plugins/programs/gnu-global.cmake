# This file is part of Notepad++ project (macOS build)
# Copyright (C)2026 Notepad++ macOS contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# GNU Global's programs, which NppGTags runs. Its Windows package carries them in bin (gtags and global,
# Universal Ctags' ctags, the plugin parsers of its ctags and pygments parsers, its gtags.conf) and in
# share/gtags/script (the pygments parser's script): here they are built from their release sources,
# with their own configure and make, for the app's architectures. Global's programs find what is beside
# them (gnu-global-relocatable.patch), as on Windows, and gtags.conf is the package's with macOS's file
# names.
#
# Included by nppmac_add_external_plugin (PROGRAMS gnu-global), in its scope: name is the plugin,
# sourceDir its checkout, and fileNames the files of its package.
include(ExternalProject)

set(gnuGlobalVersion 6.6.12)
set(gnuGlobalSha256 542a5b06840e14eca548b4bb60b44c0adcf01024e68eb362f8bf716007885901)
set(universalCtagsVersion 6.1.0)
set(universalCtagsSha256 fa3bafbd0dab0373c0c1788c79451bad4eed2095ad292a8dd46cc1ae6997b2c5)

# configure and libtool cannot work in a folder whose path has a space, which the build's may have: the
# programs are then built in the user's cache folder, in a folder of this build's.
set(programsDir "${CMAKE_CURRENT_BINARY_DIR}/${name}/programs")
if(programsDir MATCHES " ")
	execute_process(COMMAND getconf DARWIN_USER_CACHE_DIR OUTPUT_VARIABLE cacheDir OUTPUT_STRIP_TRAILING_WHITESPACE
		COMMAND_ERROR_IS_FATAL ANY)
	string(MD5 buildHash "${CMAKE_BINARY_DIR}")
	string(SUBSTRING "${buildHash}" 0 12 buildHash)
	set(programsDir "${cacheDir}nppmac-programs-${buildHash}/${name}")
endif()
set(downloadDir "${CMAKE_BINARY_DIR}/plugin-sources/downloads")
# Compiled as the app is: for its architectures and its minimum macOS, when the build names them.
set(programFlags -O2)
foreach(architecture IN LISTS CMAKE_OSX_ARCHITECTURES)
	list(APPEND programFlags -arch ${architecture})
endforeach()
if(CMAKE_OSX_DEPLOYMENT_TARGET)
	list(APPEND programFlags "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
list(JOIN programFlags " " programFlags)
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE sdkPath OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)

# Universal Ctags with macOS's libxml2 (its XML parsers: Glade, Maven2, plists, RelaxNG, SVG and XSLT,
# which gtags.conf maps), without the libraries macOS lacks (JSON and YAML output, PCRE2 regexes),
# which no parser gtags.conf maps needs. The archive's timestamps are kept, so that make does not
# run the autotools again.
ExternalProject_Add(${name}UniversalCtags
	URL "https://github.com/universal-ctags/ctags/releases/download/v${universalCtagsVersion}/universal-ctags-${universalCtagsVersion}.tar.gz"
	URL_HASH SHA256=${universalCtagsSha256}
	DOWNLOAD_DIR "${downloadDir}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	PREFIX "${programsDir}/universal-ctags"
	CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}" "CFLAGS=${programFlags}"
		"LIBXML_CFLAGS=-I${sdkPath}/usr/include/libxml2" LIBXML_LIBS=-lxml2
		<SOURCE_DIR>/configure --disable-json --disable-yaml --disable-pcre2 --disable-seccomp --disable-readcmd
		--disable-external-sort
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS <BINARY_DIR>/ctags
	LOG_CONFIGURE TRUE
	LOG_BUILD TRUE
	LOG_OUTPUT_ON_FAILURE TRUE)
ExternalProject_Get_Property(${name}UniversalCtags BINARY_DIR)
set(ctagsDir "${BINARY_DIR}")

# GNU Global with its own libltdl, without gtags-cscope (curses). Its configure is given the programs
# it would otherwise look for on this Mac: the ctags its plugin parsers run is gtags.conf's, the one
# beside them. It is not installed: the package takes its programs from its build folder, where they
# are the same, and the pygments parser's script is made executable as its install would.
ExternalProject_Add(${name}GnuGlobal
	URL "https://ftp.gnu.org/gnu/global/global-${gnuGlobalVersion}.tar.gz"
		"https://ftpmirror.gnu.org/global/global-${gnuGlobalVersion}.tar.gz"
	URL_HASH SHA256=${gnuGlobalSha256}
	DOWNLOAD_DIR "${downloadDir}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	PREFIX "${programsDir}/gnu-global"
	PATCH_COMMAND patch -p1 --forward --quiet -i "${CMAKE_CURRENT_LIST_DIR}/gnu-global-relocatable.patch"
	CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}" "CFLAGS=${programFlags}"
		<SOURCE_DIR>/configure --disable-gtagscscope --with-included-ltdl --with-universal-ctags=ctags
		--with-exuberant-ctags=no --with-posix-sort=/usr/bin/sort "--with-python-interpreter=/usr/bin/env python3"
	INSTALL_COMMAND chmod 755 plugin-factory/pygments_parser.py
	BUILD_BYPRODUCTS <BINARY_DIR>/gtags/gtags <BINARY_DIR>/global/global
	LOG_CONFIGURE TRUE
	LOG_BUILD TRUE
	LOG_OUTPUT_ON_FAILURE TRUE)
ExternalProject_Get_Property(${name}GnuGlobal BINARY_DIR)
set(globalDir "${BINARY_DIR}")

# gtags.conf names the Windows files, which Windows finds beside the programs: here they are named in
# that folder, $bindir.
file(READ "${sourceDir}/bin/bin/gtags.conf" gtagsConf)
set(macNames ctags.exe ctags universal-ctags.dll universal-ctags.so pygments-parser.dll pygments-parser.so)
while(macNames)
	list(POP_FRONT macNames windowsName macName)
	string(FIND "${gtagsConf}" "=${windowsName}:" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "Plugin ${name}: its gtags.conf no longer names ${windowsName}")
	endif()
	string(REPLACE "=${windowsName}:" "=$bindir/${macName}:" gtagsConf "${gtagsConf}")
endwhile()
set(gtagsConfFile "${CMAKE_CURRENT_BINARY_DIR}/${name}/programs/gtags.conf")
file(WRITE "${gtagsConfFile}" "${gtagsConf}")

# Copied into the package at each build (when they are rebuilt, the plugin may not be).
set(programFiles
	"${globalDir}/gtags/gtags" bin/gtags
	"${globalDir}/global/global" bin/global
	"${ctagsDir}/ctags" bin/ctags
	"${globalDir}/plugin-factory/.libs/universal-ctags.so" bin/universal-ctags.so
	"${globalDir}/plugin-factory/.libs/pygments-parser.so" bin/pygments-parser.so
	"${gtagsConfFile}" bin/gtags.conf
	"${globalDir}/plugin-factory/pygments_parser.py" share/gtags/script/pygments_parser.py)
set(copyCommands "")
while(programFiles)
	list(POP_FRONT programFiles builtFile packageFile)
	get_filename_component(packageFolder "${CMAKE_CURRENT_BINARY_DIR}/${name}/${packageFile}" DIRECTORY)
	list(APPEND copyCommands
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${packageFolder}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${builtFile}" "${CMAKE_CURRENT_BINARY_DIR}/${name}/${packageFile}")
	string(APPEND fileNames "${packageFile}\n")
endwhile()
add_custom_target(${name}Programs ${copyCommands} VERBATIM)
add_dependencies(${name}Programs ${name}UniversalCtags ${name}GnuGlobal)
add_dependencies(${name} ${name}Programs)
