// This file is part of Notepad++ project (macOS build)
// Copyright (C)2026 Notepad++ macOS contributors

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Called by the generated stubs (runtime/stubs/GenerateStubs.cmake) the first time each one runs.

#include <cstdio>
#include <cstdlib>

extern "C" void nppmacReportStub(const char* name, int* reported)
{
	if (*reported)
		return;
	*reported = 1;
	static const bool quiet = std::getenv("NPPMAC_QUIET_STUBS") != nullptr;
	if (!quiet)
		std::fprintf(stderr, "nppmac: Win32 function not implemented yet: %s\n", name);
}
