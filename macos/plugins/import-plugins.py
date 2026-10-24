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
#
# The entries are printed for catalog.cmake; each pins the checkout's current commit.

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
	project = {'type': None, 'sources': [], 'rc': [], 'includes': [], 'defines': []}
	for group in root.iter(MSBUILD + 'PropertyGroup'):
		if is_release_x64(group.get('Condition')):
			kind = group.find(MSBUILD + 'ConfigurationType')
			if kind is not None:
				project['type'] = kind.text
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
	for group in root.iter(MSBUILD + 'ItemDefinitionGroup'):
		if not is_release_x64(group.get('Condition')):
			continue
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
	# The folder of the solution that lists the project ($(SolutionDir)); the repository if none does.
	name = os.path.basename(project_path)
	for solution in glob.glob(os.path.join(checkout, '**', '*.sln'), recursive=True):
		try:
			if name in open(solution, encoding='utf-8-sig', errors='ignore').read():
				return os.path.dirname(solution)
		except OSError:
			continue
	return checkout


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
	defines = [d for d in project['defines'] if d not in WINDOWS_ONLY_DEFINES and not d.startswith('$(')]
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
	lines[-1] += ')'
	return '\n'.join(lines), None


def main():
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
