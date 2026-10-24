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
#import <PDFKit/PDFKit.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
		// The system's choice, not the application's: the program may have forced its own appearance.
		if (const char* test = std::getenv("NPPMAC_TEST_APPEARANCE"))
			return std::strcmp(test, "dark") == 0;
		return [[NSUserDefaults.standardUserDefaults stringForKey:@"AppleInterfaceStyle"] isEqualToString:@"Dark"];
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


@interface NppMacFilePanelAccessory : NSObject
{
@public
	NSPopUpButton* _fileTypes;
	std::vector<NSButton*> _checkBoxes;
}
@end

@implementation NppMacFilePanelAccessory
@end

namespace nppmac::platform
{
	namespace
	{
		NSString* toNSString(const std::string& text)
		{
			return [NSString stringWithUTF8String:text.c_str()] ?: @"";
		}

		// The file type menu and the program's check boxes, stacked under each other.
		NSView* createAccessory(const FilePanelRequest& request, NppMacFilePanelAccessory* accessory)
		{
			NSStackView* stack = [[NSStackView alloc] init];
			stack.orientation = NSUserInterfaceLayoutOrientationVertical;
			stack.alignment = NSLayoutAttributeLeading;
			stack.edgeInsets = NSEdgeInsetsMake(8, 16, 8, 16);
			stack.spacing = 6;
			if (request._save && request._fileTypeNames.size() > 1)
			{
				NSStackView* row = [[NSStackView alloc] init];
				row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
				[row addView:[NSTextField labelWithString:@"Save as type:"] inGravity:NSStackViewGravityLeading];
				NSPopUpButton* fileTypes = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
				for (const std::string& name : request._fileTypeNames)
					[fileTypes addItemWithTitle:toNSString(name)];
				// Titles must be unique in a pop-up button; repeated names are added as menu items.
				if (fileTypes.numberOfItems != static_cast<NSInteger>(request._fileTypeNames.size()))
				{
					[fileTypes removeAllItems];
					for (const std::string& name : request._fileTypeNames)
						[fileTypes.menu addItemWithTitle:toNSString(name) action:nil keyEquivalent:@""];
				}
				if (request._fileTypeIndex >= 0 && request._fileTypeIndex < fileTypes.numberOfItems)
					[fileTypes selectItemAtIndex:request._fileTypeIndex];
				[row addView:fileTypes inGravity:NSStackViewGravityLeading];
				[stack addView:row inGravity:NSStackViewGravityLeading];
				accessory->_fileTypes = fileTypes;
			}
			for (const FilePanelCheckBox& checkBox : request._checkBoxes)
			{
				NSButton* button = [NSButton checkboxWithTitle:toNSString(checkBox._label) target:nil action:nil];
				button.state = checkBox._checked ? NSControlStateValueOn : NSControlStateValueOff;
				button.enabled = checkBox._enabled;
				[stack addView:button inGravity:NSStackViewGravityLeading];
				accessory->_checkBoxes.push_back(button);
			}
			if (stack.views.count == 0)
				return nil;
			stack.frame = NSMakeRect(0, 0, 400, stack.fittingSize.height);
			return stack;
		}
	}

	FilePanelResult runFilePanel(const FilePanelRequest& request)
	{
		FilePanelResult result;
		result._fileTypeIndex = request._fileTypeIndex;
		result._checkBoxes = request._checkBoxes;
		if (const char* testPath = std::getenv("NPPMAC_TEST_FILE_PANEL"))
		{
			result._ok = *testPath != '\0';
			if (result._ok)
				result._paths.push_back(testPath);
			return result;
		}

		NSSavePanel* panel = nil;
		if (request._save)
		{
			panel = [NSSavePanel savePanel];
			panel.allowsOtherFileTypes = YES;
			panel.extensionHidden = NO;
			panel.nameFieldStringValue = toNSString(request._fileName);
		}
		else
		{
			NSOpenPanel* openPanel = [NSOpenPanel openPanel];
			openPanel.allowsMultipleSelection = request._multipleSelection;
			openPanel.canChooseDirectories = request._pickFolders;
			openPanel.canChooseFiles = !request._pickFolders;
			openPanel.canCreateDirectories = request._pickFolders;
			openPanel.resolvesAliases = YES;
			panel = openPanel;
		}
		panel.showsHiddenFiles = YES;
		if (!request._title.empty())
			panel.message = toNSString(request._title);
		if (!request._directory.empty())
			panel.directoryURL = [NSURL fileURLWithPath:toNSString(request._directory) isDirectory:YES];

		NppMacFilePanelAccessory* accessory = [[NppMacFilePanelAccessory alloc] init];
		if (NSView* view = createAccessory(request, accessory))
		{
			panel.accessoryView = view;
			if ([panel isKindOfClass:NSOpenPanel.class])
				static_cast<NSOpenPanel*>(panel).accessoryViewDisclosed = YES;
		}

		[NSApp activateIgnoringOtherApps:YES];
		if ([panel runModal] != NSModalResponseOK)
			return result;

		result._ok = true;
		if ([panel isKindOfClass:NSOpenPanel.class])
		{
			for (NSURL* url in static_cast<NSOpenPanel*>(panel).URLs)
				result._paths.push_back(url.path.UTF8String);
		}
		else if (panel.URL)
		{
			result._paths.push_back(panel.URL.path.UTF8String);
		}
		if (accessory->_fileTypes)
			result._fileTypeIndex = static_cast<int>(accessory->_fileTypes.indexOfSelectedItem);
		for (size_t i = 0; i < accessory->_checkBoxes.size(); ++i)
			result._checkBoxes[i]._checked = accessory->_checkBoxes[i].state == NSControlStateValueOn;
		return result;
	}
}

@interface NppMacColorPanelButtons : NSObject
@end

@implementation NppMacColorPanelButtons

- (void)ok:(id)sender
{
	[NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)cancel:(id)sender
{
	[NSApp stopModalWithCode:NSModalResponseCancel];
}

@end

namespace nppmac::platform
{
	bool chooseColor(unsigned& rgb)
	{
		if (const char* testColor = std::getenv("NPPMAC_TEST_COLOR"))
		{
			rgb = static_cast<unsigned>(std::strtoul(testColor, nullptr, 16)) & 0xFFFFFF;
			return true;
		}

		NSColorPanel* panel = NSColorPanel.sharedColorPanel;
		panel.showsAlpha = NO;
		panel.color = [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:1];

		// The panel has no buttons of its own; a modal run needs OK and Cancel.
		NppMacColorPanelButtons* target = [[NppMacColorPanelButtons alloc] init];
		NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:target action:@selector(cancel:)];
		cancel.keyEquivalent = @"\033";
		NSButton* ok = [NSButton buttonWithTitle:@"OK" target:target action:@selector(ok:)];
		ok.keyEquivalent = @"\r";
		NSStackView* buttons = [NSStackView stackViewWithViews:@[cancel, ok]];
		buttons.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
		buttons.frame = NSMakeRect(0, 0, 220, buttons.fittingSize.height);
		NSView* previousAccessory = panel.accessoryView;
		panel.accessoryView = buttons;

		[NSApp activateIgnoringOtherApps:YES];
		const NSModalResponse response = [NSApp runModalForWindow:panel];
		[panel orderOut:nil];
		panel.accessoryView = previousAccessory;
		if (response != NSModalResponseOK)
			return false;
		NSColor* color = [panel.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
		if (!color)
			return false;
		const auto channel = [](CGFloat value) { return static_cast<unsigned>(std::lround(std::clamp(value, 0.0, 1.0) * 255)); };
		rgb = (channel(color.redComponent) << 16) | (channel(color.greenComponent) << 8) | channel(color.blueComponent);
		return true;
	}

	namespace
	{
		NSPrintInfo* g_printInfo = nil; // the settings the last print panel chose
	}

	bool choosePrinter(PrintSetup& setup, bool showPanel)
	{
		NSPrintInfo* info = [NSPrintInfo.sharedPrintInfo copy];
		info.horizontalPagination = NSPrintingPaginationModeClip;
		info.verticalPagination = NSPrintingPaginationModeClip;
		if (showPanel && !std::getenv("NPPMAC_TEST_PRINT_PDF"))
		{
			NSPrintPanel* panel = [NSPrintPanel printPanel];
			NSPrintPanelOptions options = NSPrintPanelShowsCopies | NSPrintPanelShowsPageRange | NSPrintPanelShowsPaperSize
				| NSPrintPanelShowsOrientation | NSPrintPanelShowsScaling | NSPrintPanelShowsPreview;
			// As on Windows, a selection is what is printed unless the user says otherwise.
			if (setup._offerSelection)
			{
				options |= NSPrintPanelShowsPrintSelection;
				info.dictionary[NSPrintSelectionOnly] = @YES;
			}
			panel.options = options;
			[NSApp activateIgnoringOtherApps:YES];
			if ([panel runModalWithPrintInfo:info] != NSModalResponseOK)
				return false;
		}
		g_printInfo = info;

		const NSSize paper = info.paperSize;
		const NSRect printable = info.imageablePageBounds; // bottom-left origin
		setup._paperWidth = paper.width;
		setup._paperHeight = paper.height;
		setup._printableLeft = printable.origin.x;
		setup._printableTop = paper.height - NSMaxY(printable);
		setup._printableWidth = printable.size.width;
		setup._printableHeight = printable.size.height;
		NSMutableDictionary<NSPrintInfoAttributeKey, id>* settings = info.dictionary;
		setup._selectionOnly = setup._offerSelection && [settings[NSPrintSelectionOnly] boolValue];
		setup._pageRange = !setup._selectionOnly && settings[NSPrintAllPages] && ![settings[NSPrintAllPages] boolValue];
		if (setup._pageRange)
		{
			setup._firstPage = std::max(1, [settings[NSPrintFirstPage] intValue]);
			setup._lastPage = std::max(setup._firstPage, [settings[NSPrintLastPage] intValue]);
		}
		return true;
	}

	bool printPdf(const std::string& pdfPath, const std::string& jobName)
	{
		NSString* path = [NSString stringWithUTF8String:pdfPath.c_str()];
		if (const char* testPath = std::getenv("NPPMAC_TEST_PRINT_PDF"))
		{
			NSString* target = [NSString stringWithUTF8String:testPath];
			[NSFileManager.defaultManager removeItemAtPath:target error:nil];
			return [NSFileManager.defaultManager copyItemAtPath:path toPath:target error:nil];
		}
		PDFDocument* document = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:path]];
		if (!document)
			return false;
		// The document already holds only the pages chosen, each laid out for the paper chosen.
		NSPrintInfo* info = [g_printInfo ?: NSPrintInfo.sharedPrintInfo copy];
		info.dictionary[NSPrintAllPages] = @YES;
		info.dictionary[NSPrintSelectionOnly] = @NO;
		info.topMargin = info.bottomMargin = info.leftMargin = info.rightMargin = 0;
		NSPrintOperation* operation = [document printOperationForPrintInfo:info scalingMode:kPDFPrintPageScaleNone autoRotate:NO];
		operation.jobTitle = [NSString stringWithUTF8String:jobName.c_str()];
		operation.showsPrintPanel = NO;
		operation.showsProgressPanel = YES;
		return [operation runOperation];
	}
}
