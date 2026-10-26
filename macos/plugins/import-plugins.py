#!/usr/bin/env python3
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

# Writes catalog entries for plugins of the official plugin list from their own projects: the
# sources, resource scripts, include folders and defines of the Release x64 configuration of the
# Visual Studio project that builds the plugin's DLL. A maintainer's tool, run by hand:
#
#     import-plugins.py <plugin list (pl.x64.json)> <folder of checkouts, one per plugin name> <name>...
#     import-plugins.py --info <plugin list (pl.x64.json)> <name>...
#
# The first prints entries for catalog.cmake, each pinning the checkout's current commit; the second
# prints catalog-info.json, the list's own entries for the plugins (name, author, description...).

import glob
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

MSBUILD = '{http://schemas.microsoft.com/developer/msbuild/2003}'

# Defines that only describe a Windows build.
WINDOWS_ONLY_DEFINES = {'WIN32', '_WINDOWS', '_USRDLL', '_WINDLL', 'NDEBUG', '_CRT_SECURE_NO_WARNINGS', '_CRT_NON_CONFORMING_SWPRINTFS'}


def is_release_x64(condition):
	if not condition:
		return True
	condition = condition.replace(' ', '')
	if 'Release|x64' in condition or "'$(Platform)'=='x64'" in condition:
		return True
	return 'Release' in condition and not any(other in condition for other in ('Win32', 'x86', 'ARM', 'Debug'))


def read_project(path):
	root = ElementTree.parse(path).getroot()
	project = {'type': None, 'sources': [], 'rc': [], 'includes': [], 'defines': [], 'rc_defines': [], 'references': []}
	for group in root.iter(MSBUILD + 'PropertyGroup'):
		if is_release_x64(group.get('Condition')):
			kind = group.find(MSBUILD + 'ConfigurationType')
			if kind is not None:
				project['type'] = kind.text
			# VC++ Directories > Include Directories.
			include_path = group.find(MSBUILD + 'IncludePath')
			if include_path is not None and include_path.text:
				project['includes'] += [i.replace('\\', '/') for i in include_path.text.split(';') if i and not i.startswith('$(IncludePath)') and 'VC_' not in i]
	for group in root.iter(MSBUILD + 'ItemGroup'):
		for item in group:
			tag = item.tag.replace(MSBUILD, '')
			include = item.get('Include')
			if not include:
				continue
			include = include.replace('\\', '/')
			if tag == 'ClCompile':
				excluded = any(is_release_x64(e.get('Condition')) and (e.text or '').strip().lower() == 'true'
					for e in item.findall(MSBUILD + 'ExcludedFromBuild'))
				if not excluded:
					project['sources'].append(include)
			elif tag == 'ResourceCompile':
				project['rc'].append(include)
			elif tag == 'ProjectReference':
				project['references'].append(include)
	for group in root.iter(MSBUILD + 'ItemDefinitionGroup'):
		if not is_release_x64(group.get('Condition')):
			continue
		resource_settings = group.find(MSBUILD + 'ResourceCompile')
		if resource_settings is not None:
			rc_defines = resource_settings.find(MSBUILD + 'PreprocessorDefinitions')
			if rc_defines is not None and rc_defines.text:
				project['rc_defines'] += [d for d in rc_defines.text.split(';') if d and '%(' not in d]
		compile_settings = group.find(MSBUILD + 'ClCompile')
		if compile_settings is None:
			continue
		includes = compile_settings.find(MSBUILD + 'AdditionalIncludeDirectories')
		defines = compile_settings.find(MSBUILD + 'PreprocessorDefinitions')
		if includes is not None and includes.text:
			project['includes'] += [i.replace('\\', '/') for i in includes.text.split(';') if i and '%(' not in i]
		if defines is not None and defines.text:
			project['defines'] += [d for d in defines.text.split(';') if d and '%(' not in d]
	return project


def project_properties(path, seen=None):
	# The MSBuild properties a project defines, itself and in the .props files it imports (as Version.props
	# gives a version to its resources): their first value, conditions aside ("if not set yet").
	seen = seen if seen is not None else set()
	properties = {}
	if path in seen or not os.path.isfile(path):
		return properties
	seen.add(path)
	try:
		root = ElementTree.parse(path).getroot()
	except ElementTree.ParseError:
		return properties
	namespace = MSBUILD if root.tag.startswith(MSBUILD) else ''
	for element in root:
		tag = element.tag.replace(MSBUILD, '')
		imported = (element.get('Project') or '').replace('\\', '/')
		imported = imported.replace('$(MSBuildThisFileDirectory)', '').replace('$(MSBuildProjectDirectory)', '.')
		if tag == 'Import' and imported and '$(' not in imported:
			imported = os.path.normpath(os.path.join(os.path.dirname(path), imported))
			for key, value in project_properties(imported, seen).items():
				properties.setdefault(key, value)
		elif tag == 'PropertyGroup':
			for prop in element:
				key = prop.tag.replace(namespace, '').replace(MSBUILD, '')
				if prop.text and key not in properties:
					properties[key] = prop.text.strip()
	return properties


def expand(value, properties, depth=0):
	# $(Name) replaced by the property's value; None when one is not known.
	def replace(match):
		known = properties.get(match.group(1))
		if known is None or depth > 10:
			raise KeyError(match.group(1))
		return expand(known, properties, depth + 1)
	try:
		return re.sub(r'\$\((\w+)\)', replace, value)
	except KeyError:
		return None


def find_project(checkout, name):
	# The project that builds the plugin's DLL: the one named after the plugin, else the one with the
	# most sources, leaving out tests (repositories often carry the plugin template's project too).
	best = None
	for path in glob.glob(os.path.join(checkout, '**', '*.vcxproj'), recursive=True):
		if '/.git/' in path or re.search(r'test', os.path.relpath(path, checkout), re.IGNORECASE):
			continue
		try:
			project = read_project(path)
		except ElementTree.ParseError:
			continue
		if project['type'] != 'DynamicLibrary' or not project['sources']:
			continue
		named = os.path.splitext(os.path.basename(path))[0].lower() == name.lower()
		rank = (named, len(project['sources']))
		if best is None or rank > best[0]:
			best = (rank, path, project)
	return best[1:] if best else None


def solution_dir(checkout, project_path):
	# The folder of the solution that lists the project ($(SolutionDir)); without one, the project's own,
	# where Visual Studio puts a new solution.
	name = os.path.basename(project_path)
	for solution in glob.glob(os.path.join(checkout, '**', '*.sln'), recursive=True):
		try:
			if name in open(solution, encoding='utf-8-sig', errors='ignore').read():
				return os.path.dirname(solution)
		except OSError:
			continue
	return os.path.dirname(project_path)


def repository_path(checkout, project_dir, path, solution=None):
	# A project-relative path as a repository-relative one; None outside the repository.
	path = path.replace('$(ProjectDir)', '').replace('$(MSBuildProjectDirectory)', '.')
	if solution is not None:
		path = path.replace('$(SolutionDir)', solution + '/')
	if '$(' in path:
		return None
	full = os.path.normpath(os.path.join(project_dir, path))
	relative = os.path.relpath(full, checkout)
	return None if relative.startswith('..') else relative


def cmake_list(values):
	return ' '.join(f'"{v}"' if re.search(r'[\s;()#"]', v) else v for v in values)


def entry(name, plugin, checkout):
	found = find_project(checkout, name)
	if not found:
		return None, 'no Visual Studio project builds a DLL'
	path, project = found
	project_dir = os.path.dirname(path)
	sources = [repository_path(checkout, project_dir, s) for s in project['sources']]
	rc_files = [repository_path(checkout, project_dir, r) for r in project['rc']]
	solution = solution_dir(checkout, path)
	includes = [repository_path(checkout, project_dir, i, solution) for i in project['includes']]
	includes = [os.path.relpath(project_dir, checkout)] + [i for i in includes if i]
	# Static libraries of the same repository the project links (project references): their sources
	# are built into the plugin, with their include folders.
	for reference in project['references']:
		library_path = os.path.normpath(os.path.join(project_dir, reference))
		if not os.path.isfile(library_path) or os.path.relpath(library_path, checkout).startswith('..'):
			continue
		library = read_project(library_path)
		if library['type'] != 'StaticLibrary':
			continue
		library_dir = os.path.dirname(library_path)
		sources += [repository_path(checkout, library_dir, s) for s in library['sources']]
		library_includes = [repository_path(checkout, library_dir, i, solution) for i in library['includes']]
		includes += [os.path.relpath(library_dir, checkout)] + [i for i in library_includes if i]
	defines = [d for d in project['defines'] if d not in WINDOWS_ONLY_DEFINES and not d.startswith('$(')]
	properties = project_properties(path)
	rc_defines = [expand(d, properties) for d in project['rc_defines'] if d not in WINDOWS_ONLY_DEFINES]
	rc_defines = [d for d in rc_defines if d and d not in ('_UNICODE', 'UNICODE')]
	if None in sources or None in rc_files:
		return None, 'sources outside the repository'
	remote = subprocess.run(['git', '-C', checkout, 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip()
	commit = subprocess.run(['git', '-C', checkout, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
	lines = [f'nppmac_add_external_plugin({name}',
		f'\tDISPLAY_NAME {cmake_list([plugin["display-name"]])}',
		f'\tVERSION {plugin["version"]}',
		f'\tGIT {remote}',
		f'\tCOMMIT {commit}',
		f'\tSOURCES {cmake_list(sources)}']
	if rc_files:
		lines.append(f'\tRC_FILES {cmake_list(rc_files)}')
	lines.append(f'\tINCLUDES {cmake_list(sorted(set(includes), key=includes.index))}')
	if defines:
		lines.append(f'\tDEFINES {cmake_list(defines)}')
	if rc_defines:
		lines.append(f'\tRC_DEFINES {cmake_list(rc_defines)}')
	lines[-1] += ')'
	return '\n'.join(lines), None


def write_info(plugin_list, names):
	# The official list's entries for the catalog's plugins (catalog-info.json), which the build turns into
	# the plugin list Plugins Admin reads.
	plugins = {p['folder-name']: p for p in json.load(open(plugin_list, encoding='utf-8-sig'))['npp-plugins']}
	info = {name: {k: v for k, v in plugins[name].items() if k not in ('id', 'repository')} for name in sorted(names, key=str.lower)}
	print(json.dumps(info, indent='\t', ensure_ascii=False))


def main():
	if len(sys.argv) >= 3 and sys.argv[1] == '--info':
		write_info(sys.argv[2], sys.argv[3:])
		return
	if len(sys.argv) < 4:
		sys.exit(__doc__ or 'usage: import-plugins.py <pl.x64.json> <checkouts> <name>...')
	plugins = {p['folder-name']: p for p in json.load(open(sys.argv[1], encoding='utf-8-sig'))['npp-plugins']}
	for name in sys.argv[3:]:
		text, problem = entry(name, plugins[name], os.path.join(sys.argv[2], name))
		if problem:
			print(f'# {name}: {problem}', file=sys.stderr)
		else:
			print(text + '\n')


if __name__ == '__main__':
	main()
