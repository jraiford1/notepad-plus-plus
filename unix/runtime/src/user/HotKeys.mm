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

// System-wide hot keys (RegisterHotKey) with Carbon's RegisterEventHotKey, which, unlike watching every
// key event, needs no accessibility permission.

#import <Carbon/Carbon.h>

#include "Backend.h"

#include <cstdlib>
#include <map>
#include <utility>

namespace
{
	struct HotKey
	{
		EventHotKeyRef _ref = nullptr;
		std::function<void()> _pressed;
	};

	std::map<UInt32, HotKey>& hotKeys()
	{
		static auto* keys = new std::map<UInt32, HotKey>;
		return *keys;
	}

	OSStatus hotKeyPressed(EventHandlerCallRef, EventRef event, void*)
	{
		EventHotKeyID id{};
		if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr)
			return eventNotHandledErr;
		auto it = hotKeys().find(id.id);
		if (it == hotKeys().end() || !it->second._pressed)
			return eventNotHandledErr;
		// A copy: the function may unregister the key.
		const std::function<void()> pressed = it->second._pressed;
		pressed();
		return noErr;
	}
}

namespace nppmac::ui
{
	unsigned registerHotKey(unsigned short keyCode, unsigned modifiers, std::function<void()> pressed)
	{
		static UInt32 nextToken = 1;
		const UInt32 token = nextToken++;
		// A test run never takes keys from the user's other programs: the key is not registered.
		if (std::getenv("NPPMAC_TEST_RUN"))
		{
			hotKeys()[token] = HotKey{};
			return token;
		}
		static const bool installed = []
		{
			const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
			return InstallApplicationEventHandler(NewEventHandlerUPP(hotKeyPressed), 1, &type, nullptr, nullptr) == noErr;
		}();
		if (!installed)
			return 0;
		UInt32 carbonModifiers = 0;
		if (modifiers & hotKeyCommand)
			carbonModifiers |= cmdKey;
		if (modifiers & hotKeyOption)
			carbonModifiers |= optionKey;
		if (modifiers & hotKeyControl)
			carbonModifiers |= controlKey;
		if (modifiers & hotKeyShift)
			carbonModifiers |= shiftKey;
		EventHotKeyRef ref = nullptr;
		if (RegisterEventHotKey(keyCode, carbonModifiers, EventHotKeyID{'NPPh', token}, GetApplicationEventTarget(), 0, &ref) != noErr)
			return 0;
		hotKeys()[token] = HotKey{ref, std::move(pressed)};
		return token;
	}

	void unregisterHotKey(unsigned token)
	{
		auto it = hotKeys().find(token);
		if (it == hotKeys().end())
			return;
		if (it->second._ref)
			UnregisterEventHotKey(it->second._ref);
		hotKeys().erase(it);
	}
}
