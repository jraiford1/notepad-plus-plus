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

// The Common Item Dialog (CLSID_FileOpenDialog, CLSID_FileSaveDialog) on NSOpenPanel and
// NSSavePanel, with the shell items it works with, and CoCreateInstance for the classes the runtime
// provides. Show() runs the panel; the dialog keeps the state the program sets and reads back.

#include "kernel/Internal.h"
#include "platform/Platform.h"

#include <shobjidl.h>

#include <atomic>
#include <string>
#include <unistd.h>
#include <vector>

using nppmac::kernel::fromPosixPath;
using nppmac::kernel::toPosixPath;

namespace
{
	LPWSTR duplicateForCaller(std::u16string_view text)
	{
		auto* copy = static_cast<LPWSTR>(CoTaskMemAlloc((text.size() + 1) * sizeof(char16_t)));
		if (copy)
		{
			std::copy(text.begin(), text.end(), copy);
			copy[text.size()] = u'\0';
		}
		return copy;
	}

	std::u16string fileNamePart(const std::u16string& path)
	{
		const size_t slash = path.find_last_of(u"/\\");
		return slash == std::u16string::npos ? path : path.substr(slash + 1);
	}

	std::u16string parentPart(const std::u16string& path)
	{
		const size_t slash = path.find_last_of(u"/\\");
		if (slash == std::u16string::npos)
			return {};
		return slash == 0 ? u"/" : path.substr(0, slash);
	}

	// A file system item: the only kind of shell item here.
	class FileSystemItem final : public IShellItem
	{
	public:
		explicit FileSystemItem(std::u16string path) : _path{std::move(path)} {}

		const std::u16string& path() const { return _path; }

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
		{
			if (!object)
				return E_POINTER;
			if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellItem))
			{
				*object = static_cast<IShellItem*>(this);
				AddRef();
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG references = --_references;
			if (references == 0)
				delete this;
			return references;
		}

		HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*, REFGUID, REFIID, void** object) override
		{
			if (object)
				*object = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetParent(IShellItem** parent) override
		{
			if (!parent)
				return E_POINTER;
			const std::u16string parentPath = parentPart(_path);
			if (parentPath.empty() || parentPath == _path)
			{
				*parent = nullptr;
				return E_FAIL;
			}
			*parent = new FileSystemItem{parentPath};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN form, LPWSTR* name) override
		{
			if (!name)
				return E_POINTER;
			const bool nameOnly = form == SIGDN_NORMALDISPLAY || form == SIGDN_PARENTRELATIVE || form == SIGDN_PARENTRELATIVEPARSING
				|| form == SIGDN_PARENTRELATIVEEDITING || form == SIGDN_PARENTRELATIVEFORADDRESSBAR || form == SIGDN_PARENTRELATIVEFORUI;
			*name = duplicateForCaller(nameOnly ? fileNamePart(_path) : _path);
			return *name ? S_OK : E_OUTOFMEMORY;
		}

		HRESULT STDMETHODCALLTYPE GetAttributes(SFGAOF mask, SFGAOF* attributes) override
		{
			if (!attributes)
				return E_POINTER;
			SFGAOF found = SFGAO_FILESYSTEM;
			const std::string posixPath = toPosixPath(_path);
			struct stat info{};
			if (::stat(posixPath.c_str(), &info) == 0)
			{
				if (S_ISDIR(info.st_mode))
					found |= SFGAO_FOLDER | SFGAO_FILESYSANCESTOR;
				if (::access(posixPath.c_str(), W_OK) != 0)
					found |= SFGAO_READONLY;
			}
			*attributes = found & mask;
			return *attributes == mask ? S_OK : S_FALSE;
		}

		HRESULT STDMETHODCALLTYPE Compare(IShellItem* other, SICHINTF, int* order) override
		{
			if (!other || !order)
				return E_POINTER;
			LPWSTR otherPath = nullptr;
			if (FAILED(other->GetDisplayName(SIGDN_FILESYSPATH, &otherPath)))
				return E_FAIL;
			*order = _path.compare(otherPath);
			CoTaskMemFree(otherPath);
			return *order == 0 ? S_OK : S_FALSE;
		}

	private:
		std::u16string _path;
		std::atomic<ULONG> _references{1};
	};

	class FileSystemItemArray final : public IShellItemArray
	{
	public:
		explicit FileSystemItemArray(std::vector<std::u16string> paths) : _paths{std::move(paths)} {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
		{
			if (!object)
				return E_POINTER;
			if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellItemArray))
			{
				*object = static_cast<IShellItemArray*>(this);
				AddRef();
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG references = --_references;
			if (references == 0)
				delete this;
			return references;
		}

		HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*, REFGUID, REFIID, void** object) override
		{
			if (object)
				*object = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetPropertyStore(GETPROPERTYSTOREFLAGS, REFIID, void** object) override
		{
			if (object)
				*object = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetPropertyDescriptionList(REFPROPERTYKEY, REFIID, void** object) override
		{
			if (object)
				*object = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetAttributes(SIATTRIBFLAGS, SFGAOF, SFGAOF* attributes) override
		{
			if (attributes)
				*attributes = 0;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetCount(DWORD* count) override
		{
			if (!count)
				return E_POINTER;
			*count = static_cast<DWORD>(_paths.size());
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetItemAt(DWORD index, IShellItem** item) override
		{
			if (!item)
				return E_POINTER;
			if (index >= _paths.size())
			{
				*item = nullptr;
				return E_INVALIDARG;
			}
			*item = new FileSystemItem{_paths[index]};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE EnumItems(IEnumShellItems** items) override
		{
			if (items)
				*items = nullptr;
			return E_NOTIMPL;
		}

	private:
		std::vector<std::u16string> _paths;
		std::atomic<ULONG> _references{1};
	};

	std::u16string pathOf(IShellItem* item)
	{
		std::u16string path;
		LPWSTR name = nullptr;
		if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name)
		{
			path = name;
			CoTaskMemFree(name);
		}
		return path;
	}

	struct CheckButton
	{
		DWORD _id = 0;
		std::u16string _label;
		bool _checked = false;
		CDCONTROLSTATEF _state = CDCS_ENABLEDVISIBLE;
	};

	// What both dialogs share: IFileDialog and IFileDialogCustomize (check buttons only).
	template <typename DialogInterface>
	class FileDialogBase : public DialogInterface, public IFileDialogCustomize
	{
	public:
		virtual ~FileDialogBase()
		{
			for (IFileDialogEvents* events : _events)
			{
				if (events)
					events->Release();
			}
		}

		// IUnknown

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
		{
			if (!object)
				return E_POINTER;
			if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IModalWindow) || IsEqualIID(riid, IID_IFileDialog)
				|| IsEqualIID(riid, dialogInterfaceId()))
			{
				*object = static_cast<DialogInterface*>(this);
			}
			else if (IsEqualIID(riid, IID_IFileDialogCustomize))
			{
				*object = static_cast<IFileDialogCustomize*>(this);
			}
			else
			{
				*object = nullptr;
				return E_NOINTERFACE;
			}
			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++_references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG references = --_references;
			if (references == 0)
				delete this;
			return references;
		}

		// IModalWindow

		HRESULT STDMETHODCALLTYPE Show(HWND owner) override
		{
			nppmac::platform::FilePanelRequest request;
			request._save = isSaveDialog();
			request._multipleSelection = (_options & FOS_ALLOWMULTISELECT) != 0;
			request._pickFolders = (_options & FOS_PICKFOLDERS) != 0;
			request._title = nppmac::toUtf8(_title);
			request._directory = toPosixPath(!_folder.empty() ? _folder : _defaultFolder);
			request._fileName = nppmac::toUtf8(_fileName);
			for (const std::u16string& name : _fileTypeNames)
				request._fileTypeNames.push_back(nppmac::toUtf8(name));
			request._fileTypeIndex = _fileTypeIndex > 0 ? static_cast<int>(_fileTypeIndex) - 1 : 0;
			for (const CheckButton& checkButton : _checkButtons)
			{
				if (checkButton._state & CDCS_VISIBLE)
					request._checkBoxes.push_back({static_cast<int>(checkButton._id), nppmac::toUtf8(checkButton._label), checkButton._checked, (checkButton._state & CDCS_ENABLED) != 0});
			}

			// The panel is modal, like the Windows dialog, so the owner is disabled meanwhile.
			const bool ownerWasEnabled = owner && IsWindowEnabled(owner);
			if (ownerWasEnabled)
				EnableWindow(owner, FALSE);
			const nppmac::platform::FilePanelResult result = nppmac::platform::runFilePanel(request);
			if (ownerWasEnabled)
				EnableWindow(owner, TRUE);

			if (!result._ok || result._paths.empty())
				return HRESULT_FROM_WIN32(ERROR_CANCELLED);

			_results.clear();
			for (const std::string& path : result._paths)
				_results.push_back(fromPosixPath(path));
			if (isSaveDialog() && !_defaultExtension.empty() && fileNamePart(_results[0]).find(u'.') == std::u16string::npos)
				_results[0] += u"." + _defaultExtension;
			_fileName = fileNamePart(_results[0]);
			_folder = request._pickFolders ? _results[0] : parentPart(_results[0]);
			if (!_fileTypeNames.empty())
				_fileTypeIndex = static_cast<UINT>(result._fileTypeIndex) + 1;
			for (const nppmac::platform::FilePanelCheckBox& checkBox : result._checkBoxes)
			{
				if (CheckButton* checkButton = findCheckButton(static_cast<DWORD>(checkBox._id)))
					checkButton->_checked = checkBox._checked;
			}

			for (IFileDialogEvents* events : _events)
			{
				if (events)
					events->OnFileOk(static_cast<DialogInterface*>(this));
			}
			return S_OK;
		}

		// IFileDialog

		HRESULT STDMETHODCALLTYPE SetFileTypes(UINT count, const COMDLG_FILTERSPEC* types) override
		{
			if (!types && count)
				return E_INVALIDARG;
			_fileTypeNames.clear();
			for (UINT i = 0; i < count; ++i)
				_fileTypeNames.push_back(types[i].pszName ? types[i].pszName : u"");
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetFileTypeIndex(UINT index) override
		{
			_fileTypeIndex = index;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFileTypeIndex(UINT* index) override
		{
			if (!index)
				return E_POINTER;
			*index = _fileTypeIndex;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE Advise(IFileDialogEvents* events, DWORD* cookie) override
		{
			if (!events || !cookie)
				return E_INVALIDARG;
			events->AddRef();
			_events.push_back(events);
			*cookie = static_cast<DWORD>(_events.size());
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override
		{
			if (cookie == 0 || cookie > _events.size() || !_events[cookie - 1])
				return E_INVALIDARG;
			_events[cookie - 1]->Release();
			_events[cookie - 1] = nullptr;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetOptions(FILEOPENDIALOGOPTIONS options) override
		{
			_options = options;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetOptions(FILEOPENDIALOGOPTIONS* options) override
		{
			if (!options)
				return E_POINTER;
			*options = _options;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetDefaultFolder(IShellItem* item) override
		{
			_defaultFolder = pathOf(item);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetFolder(IShellItem* item) override
		{
			_folder = pathOf(item);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFolder(IShellItem** item) override
		{
			if (!item)
				return E_POINTER;
			const std::u16string& folder = !_folder.empty() ? _folder : _defaultFolder;
			if (folder.empty())
			{
				*item = nullptr;
				return E_FAIL;
			}
			*item = new FileSystemItem{folder};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetCurrentSelection(IShellItem** item) override
		{
			return GetResult(item);
		}

		HRESULT STDMETHODCALLTYPE SetFileName(LPCWSTR name) override
		{
			_fileName = name ? name : u"";
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFileName(LPWSTR* name) override
		{
			if (!name)
				return E_POINTER;
			*name = duplicateForCaller(_fileName);
			return *name ? S_OK : E_OUTOFMEMORY;
		}

		HRESULT STDMETHODCALLTYPE SetTitle(LPCWSTR title) override
		{
			_title = title ? title : u"";
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetOkButtonLabel(LPCWSTR) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetFileNameLabel(LPCWSTR) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetResult(IShellItem** item) override
		{
			if (!item)
				return E_POINTER;
			if (_results.empty())
			{
				*item = nullptr;
				return E_UNEXPECTED;
			}
			*item = new FileSystemItem{_results[0]};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE AddPlace(IShellItem*, FDAP) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetDefaultExtension(LPCWSTR extension) override
		{
			_defaultExtension = extension ? extension : u"";
			if (!_defaultExtension.empty() && _defaultExtension[0] == u'.')
				_defaultExtension.erase(0, 1);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE Close(HRESULT) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetClientGuid(REFGUID) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE ClearClientData() override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetFilter(IShellItemFilter*) override
		{
			return S_OK;
		}

		// IFileDialogCustomize: check buttons; the other controls are not offered.

		HRESULT STDMETHODCALLTYPE EnableOpenDropDown(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddMenu(DWORD, LPCWSTR) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddPushButton(DWORD, LPCWSTR) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddComboBox(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddRadioButtonList(DWORD) override { return E_NOTIMPL; }

		HRESULT STDMETHODCALLTYPE AddCheckButton(DWORD id, LPCWSTR label, BOOL checked) override
		{
			if (findCheckButton(id))
				return E_INVALIDARG;
			_checkButtons.push_back({id, label ? label : u"", checked != FALSE});
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE AddEditBox(DWORD, LPCWSTR) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddSeparator(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE AddText(DWORD, LPCWSTR) override { return E_NOTIMPL; }

		HRESULT STDMETHODCALLTYPE SetControlLabel(DWORD id, LPCWSTR label) override
		{
			CheckButton* checkButton = findCheckButton(id);
			if (!checkButton)
				return E_INVALIDARG;
			checkButton->_label = label ? label : u"";
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetControlState(DWORD id, CDCONTROLSTATEF* state) override
		{
			CheckButton* checkButton = findCheckButton(id);
			if (!checkButton || !state)
				return E_INVALIDARG;
			*state = checkButton->_state;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetControlState(DWORD id, CDCONTROLSTATEF state) override
		{
			CheckButton* checkButton = findCheckButton(id);
			if (!checkButton)
				return E_INVALIDARG;
			checkButton->_state = state;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetEditBoxText(DWORD, WCHAR** text) override
		{
			if (text)
				*text = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE SetEditBoxText(DWORD, LPCWSTR) override { return E_NOTIMPL; }

		HRESULT STDMETHODCALLTYPE GetCheckButtonState(DWORD id, BOOL* checked) override
		{
			CheckButton* checkButton = findCheckButton(id);
			if (!checkButton || !checked)
				return E_INVALIDARG;
			*checked = checkButton->_checked ? TRUE : FALSE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetCheckButtonState(DWORD id, BOOL checked) override
		{
			CheckButton* checkButton = findCheckButton(id);
			if (!checkButton)
				return E_INVALIDARG;
			checkButton->_checked = checked != FALSE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE AddControlItem(DWORD, DWORD, LPCWSTR) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE RemoveControlItem(DWORD, DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE RemoveAllControlItems(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetControlItemState(DWORD, DWORD, CDCONTROLSTATEF*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetControlItemState(DWORD, DWORD, CDCONTROLSTATEF) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetSelectedControlItem(DWORD, DWORD*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetSelectedControlItem(DWORD, DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE StartVisualGroup(DWORD, LPCWSTR) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE EndVisualGroup() override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE MakeProminent(DWORD) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetControlItemText(DWORD, DWORD, LPCWSTR) override { return E_NOTIMPL; }

	protected:
		virtual bool isSaveDialog() const = 0;
		virtual REFIID dialogInterfaceId() const = 0;

		std::vector<std::u16string> _results;

	private:
		CheckButton* findCheckButton(DWORD id)
		{
			for (CheckButton& checkButton : _checkButtons)
			{
				if (checkButton._id == id)
					return &checkButton;
			}
			return nullptr;
		}

		std::atomic<ULONG> _references{1};
		FILEOPENDIALOGOPTIONS _options = 0;
		std::u16string _title;
		std::u16string _folder;
		std::u16string _defaultFolder;
		std::u16string _fileName;
		std::u16string _defaultExtension;
		std::vector<std::u16string> _fileTypeNames;
		UINT _fileTypeIndex = 1; // 1-based, as in the Windows interface
		std::vector<CheckButton> _checkButtons;
		std::vector<IFileDialogEvents*> _events; // indexed by cookie - 1; null once unadvised
	};

	class PanelOpenDialog final : public FileDialogBase<IFileOpenDialog>
	{
	public:
		HRESULT STDMETHODCALLTYPE GetResults(IShellItemArray** items) override
		{
			if (!items)
				return E_POINTER;
			if (_results.empty())
			{
				*items = nullptr;
				return E_UNEXPECTED;
			}
			*items = new FileSystemItemArray{_results};
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetSelectedItems(IShellItemArray** items) override
		{
			return GetResults(items);
		}

	protected:
		bool isSaveDialog() const override { return false; }
		REFIID dialogInterfaceId() const override { return IID_IFileOpenDialog; }
	};

	class PanelSaveDialog final : public FileDialogBase<IFileSaveDialog>
	{
	public:
		HRESULT STDMETHODCALLTYPE SetSaveAsItem(IShellItem* item) override
		{
			const std::u16string path = pathOf(item);
			if (path.empty())
				return E_INVALIDARG;
			SetFileName(fileNamePart(path).c_str());
			FileSystemItem folder{parentPart(path)};
			SetFolder(&folder);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetProperties(IPropertyStore*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetCollectedProperties(IPropertyDescriptionList*, BOOL) override { return E_NOTIMPL; }

		HRESULT STDMETHODCALLTYPE GetProperties(IPropertyStore** store) override
		{
			if (store)
				*store = nullptr;
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE ApplyProperties(IShellItem*, IPropertyStore*, HWND, IFileOperationProgressSink*) override { return E_NOTIMPL; }

	protected:
		bool isSaveDialog() const override { return true; }
		REFIID dialogInterfaceId() const override { return IID_IFileSaveDialog; }
	};

	template <typename Object>
	HRESULT createAndQuery(REFIID riid, LPVOID* object)
	{
		auto* created = new Object;
		const HRESULT result = created->QueryInterface(riid, object);
		created->Release();
		return result;
	}
}

// Only the classes above exist; for any other, callers take their fallback paths, as on a Windows
// without the class.
HRESULT WINAPI CoCreateInstance(REFCLSID classId, LPUNKNOWN outer, DWORD, REFIID riid, LPVOID* object)
{
	if (!object)
		return E_POINTER;
	*object = nullptr;
	if (outer)
		return CLASS_E_NOAGGREGATION;
	if (IsEqualCLSID(classId, CLSID_FileOpenDialog))
		return createAndQuery<PanelOpenDialog>(riid, object);
	if (IsEqualCLSID(classId, CLSID_FileSaveDialog))
		return createAndQuery<PanelSaveDialog>(riid, object);
	return REGDB_E_CLASSNOTREG;
}

HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR path, IBindCtx*, REFIID riid, void** item)
{
	if (!item)
		return E_POINTER;
	*item = nullptr;
	if (!path || !*path)
		return E_INVALIDARG;
	// As on Windows, the item must exist.
	struct stat info{};
	if (::stat(toPosixPath(path).c_str(), &info) != 0)
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	auto* created = new FileSystemItem{fromPosixPath(toPosixPath(path))};
	const HRESULT result = created->QueryInterface(riid, item);
	created->Release();
	return result;
}
