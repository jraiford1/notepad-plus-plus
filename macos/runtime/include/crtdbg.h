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

// The Microsoft CRT's <crtdbg.h>: the debug library's assertions, reports and heap checks, as its
// release library has them (nothing), which is the one plugins are built against.

#pragma once

#include <stddef.h>

#define _CRT_WARN 0
#define _CRT_ERROR 1
#define _CRT_ASSERT 2

#define _CRTDBG_ALLOC_MEM_DF 0x01
#define _CRTDBG_DELAY_FREE_MEM_DF 0x02
#define _CRTDBG_CHECK_ALWAYS_DF 0x04
#define _CRTDBG_CHECK_CRT_DF 0x10
#define _CRTDBG_LEAK_CHECK_DF 0x20
#define _CRTDBG_MODE_FILE 0x1
#define _CRTDBG_MODE_DEBUG 0x2
#define _CRTDBG_MODE_WNDW 0x4
#define _CRTDBG_REPORT_MODE -1

typedef struct _CrtMemState
{
	struct _CrtMemBlockHeader* pBlockHeader;
	size_t lCounts[5];
	size_t lSizes[5];
	size_t lHighWaterCount;
	size_t lTotalCount;
} _CrtMemState;

#ifndef _ASSERT
#define _ASSERT(expr) ((void)0)
#endif
#ifndef _ASSERTE
#define _ASSERTE(expr) ((void)0)
#endif
#ifndef _ASSERT_EXPR
#define _ASSERT_EXPR(expr, message) ((void)0)
#endif

#define _RPT0(...) ((void)0)
#define _RPT1(...) ((void)0)
#define _RPT2(...) ((void)0)
#define _RPT3(...) ((void)0)
#define _RPT4(...) ((void)0)
#define _RPT5(...) ((void)0)
#define _RPT6(...) ((void)0)
#define _RPTF0(...) ((void)0)
#define _RPTF1(...) ((void)0)
#define _RPTF2(...) ((void)0)
#define _RPTF3(...) ((void)0)
#define _RPTF4(...) ((void)0)
#define _RPTF5(...) ((void)0)
#define _RPTW0(...) ((void)0)
#define _RPTW1(...) ((void)0)
#define _RPTW2(...) ((void)0)
#define _RPTW3(...) ((void)0)
#define _RPTW4(...) ((void)0)
#define _RPTW5(...) ((void)0)

#define _CrtSetDbgFlag(flag) ((int)0)
#define _CrtSetReportMode(type, mode) ((int)0)
#define _CrtSetReportFile(type, file) ((void*)0)
#define _CrtDumpMemoryLeaks() ((int)0)
#define _CrtCheckMemory() ((int)1)
#define _CrtMemCheckpoint(state) ((void)0)
#define _CrtMemDifference(difference, old, recent) ((int)0)
#define _CrtMemDumpStatistics(state) ((void)0)
#define _CrtMemDumpAllObjectsSince(state) ((void)0)
