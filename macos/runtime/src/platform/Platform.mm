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

#import <Cocoa/Cocoa.h>

#include <unistd.h>

#include "Platform.h"

namespace nppmac::platform
{
	namespace
	{
		std::string toString(NSString* string)
		{
			return string ? std::string{string.UTF8String} : std::string{};
		}

		std::string directory(NSSearchPathDirectory which)
		{
			NSArray<NSURL*>* urls = [NSFileManager.defaultManager URLsForDirectory:which inDomains:NSUserDomainMask];
			return urls.count > 0 ? toString(urls.firstObject.path) : std::string{};
		}

		NSURL* fileOrUrl(const std::string& urlOrPath)
		{
			NSString* string = [NSString stringWithUTF8String:urlOrPath.c_str()];
			if (string.length > 0 && [string characterAtIndex:0] == '/')
				return [NSURL fileURLWithPath:string];
			NSURL* url = [NSURL URLWithString:string];
			return url.scheme ? url : [NSURL fileURLWithPath:string];
		}
	}

	std::string preferredLanguage()
	{
		NSArray<NSString*>* languages = NSLocale.preferredLanguages;
		return languages.count > 0 ? toString(languages.firstObject) : std::string{"en-US"};
	}

	bool isDarkAppearance()
	{
		NSAppearance* appearance = NSApp ? NSApp.effectiveAppearance : NSAppearance.currentDrawingAppearance;
		NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
		return [match isEqualToString:NSAppearanceNameDarkAqua];
	}

	std::string executablePath()
	{
		return toString(NSBundle.mainBundle.executablePath);
	}

	std::string resourcesDirectory()
	{
		return toString(NSBundle.mainBundle.resourcePath);
	}

	std::string applicationSupportDirectory()
	{
		return directory(NSApplicationSupportDirectory);
	}

	std::string documentsDirectory()
	{
		return directory(NSDocumentDirectory);
	}

	std::string desktopDirectory()
	{
		return directory(NSDesktopDirectory);
	}

	std::string homeDirectory()
	{
		return toString(NSHomeDirectory());
	}

	std::string userName()
	{
		return toString(NSUserName());
	}

	std::string computerName()
	{
		NSString* name = NSHost.currentHost.localizedName;
		return name ? toString(name) : std::string{"Mac"};
	}

	bool openUrlOrFile(const std::string& urlOrPath, const std::string& applicationPath)
	{
		NSURL* url = fileOrUrl(urlOrPath);
		if (!url)
			return false;
		if (!applicationPath.empty())
		{
			NSURL* application = [NSURL fileURLWithPath:[NSString stringWithUTF8String:applicationPath.c_str()]];
			[NSWorkspace.sharedWorkspace openURLs:@[url] withApplicationAtURL:application
				configuration:NSWorkspaceOpenConfiguration.configuration completionHandler:nil];
			return true;
		}
		return [NSWorkspace.sharedWorkspace openURL:url];
	}

	bool revealInFinder(const std::vector<std::string>& paths)
	{
		NSMutableArray<NSURL*>* urls = [NSMutableArray array];
		for (const std::string& path : paths)
			[urls addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]];
		[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:urls];
		return true;
	}

	bool moveToTrash(const std::string& path)
	{
		NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
		return [NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:nil error:nil];
	}

	void beep()
	{
		NSBeep();
	}

	long pasteboardChangeCount()
	{
		return NSPasteboard.generalPasteboard.changeCount;
	}

	std::vector<std::string> pasteboardTypes()
	{
		std::vector<std::string> result;
		for (NSPasteboardType type in NSPasteboard.generalPasteboard.types)
			result.push_back(type.UTF8String);
		return result;
	}

	std::vector<unsigned char> pasteboardData(const std::string& type)
	{
		NSData* data = [NSPasteboard.generalPasteboard dataForType:[NSString stringWithUTF8String:type.c_str()]];
		if (!data)
			return {};
		const auto* bytes = static_cast<const unsigned char*>(data.bytes);
		return std::vector<unsigned char>(bytes, bytes + data.length);
	}

	void setPasteboard(const std::vector<std::pair<std::string, std::vector<unsigned char>>>& items)
	{
		NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
		NSMutableArray<NSPasteboardType>* types = [NSMutableArray array];
		for (const auto& [type, data] : items)
			[types addObject:[NSString stringWithUTF8String:type.c_str()]];
		[pasteboard declareTypes:types owner:nil];
		for (const auto& [type, data] : items)
			[pasteboard setData:[NSData dataWithBytes:data.data() length:data.size()] forType:[NSString stringWithUTF8String:type.c_str()]];
	}

	int showAlert(const std::string& title, const std::string& message, const std::vector<std::string>& buttons,
		AlertStyle style, int defaultButton, int escapeButton)
	{
		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = [NSString stringWithUTF8String:title.c_str()];
		alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
		alert.alertStyle = style == AlertStyle::critical ? NSAlertStyleCritical
			: (style == AlertStyle::warning ? NSAlertStyleWarning : NSAlertStyleInformational);

		// The default button goes first (rightmost, Return); the others keep their Windows order.
		std::vector<int> order;
		order.push_back(defaultButton);
		for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
		{
			if (i != defaultButton)
				order.push_back(i);
		}
		for (const int index : order)
		{
			NSButton* button = [alert addButtonWithTitle:[NSString stringWithUTF8String:buttons[static_cast<size_t>(index)].c_str()]];
			if (index == escapeButton && index != defaultButton)
				button.keyEquivalent = @"\033";
		}

		[NSApp activateIgnoringOtherApps:YES];
		const NSModalResponse response = [alert runModal];
		const int position = static_cast<int>(response - NSAlertFirstButtonReturn);
		return position >= 0 && position < static_cast<int>(order.size()) ? order[static_cast<size_t>(position)] : escapeButton;
	}
}
