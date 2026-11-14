// This file is part of Notepad++ project (macOS and Linux build)
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

// The Linux side of Platform.h: POSIX, /proc and /sys, and Qt for what it has (paths, locales, Unicode
// normalization, the trash). Compiled without the Win32 headers, as the Mac's .mm side is.
//
// The services that need windows (alerts, the file, color and font choosers, the clipboard, printing)
// answer their test settings (NPPMAC_TEST_*) and otherwise report failure, until J4's dialogs step.

#include "../Platform.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>
#include <QtGui/QStyleHints>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <fstream>
#include <malloc.h>
#include <pwd.h>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace nppmac::platform
{
	namespace
	{
		std::string toString(const QString& string)
		{
			return string.toStdString();
		}

		QString toQString(const std::string& string)
		{
			return QString::fromStdString(string);
		}

		std::string readFile(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}

		std::string trim(std::string text)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.pop_back();
			size_t start = 0;
			while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
				++start;
			return text.substr(start);
		}

		// A value of /etc/os-release (NAME, VERSION_ID...), unquoted.
		std::string osRelease(std::string_view key)
		{
			std::istringstream lines(readFile("/etc/os-release"));
			for (std::string line; std::getline(lines, line);)
			{
				if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
				{
					std::string value = line.substr(key.size() + 1);
					if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
						value = value.substr(1, value.size() - 2);
					return value;
				}
			}
			return {};
		}

		const passwd* account()
		{
			return getpwuid(getuid());
		}

		bool testRun()
		{
			return std::getenv("NPPMAC_TEST_RUN") != nullptr;
		}

		// Windows locale identifiers (LCID) by locale name: Windows' own list, for the languages of
		// Notepad++'s translations and their common regions. The first entry of a language is its default.
		struct LocaleCode
		{
			const char* _name;
			unsigned _lcid;
		};

		constexpr LocaleCode localeCodes[] = {
			{"af-ZA", 0x0436}, {"am-ET", 0x045E}, {"ar-SA", 0x0401}, {"ar-EG", 0x0C01}, {"ar-DZ", 0x1401},
			{"ar-MA", 0x1801}, {"ar-AE", 0x3801}, {"az-Latn-AZ", 0x042C}, {"be-BY", 0x0423}, {"bg-BG", 0x0402},
			{"bn-IN", 0x0445}, {"bn-BD", 0x0845}, {"br-FR", 0x047E}, {"bs-Latn-BA", 0x141A}, {"ca-ES", 0x0403},
			{"co-FR", 0x0483}, {"cs-CZ", 0x0405}, {"cy-GB", 0x0452}, {"da-DK", 0x0406}, {"de-DE", 0x0407},
			{"de-CH", 0x0807}, {"de-AT", 0x0C07}, {"de-LU", 0x1007}, {"de-LI", 0x1407}, {"el-GR", 0x0408},
			{"en-US", 0x0409}, {"en-GB", 0x0809}, {"en-AU", 0x0C09}, {"en-CA", 0x1009}, {"en-NZ", 0x1409},
			{"en-IE", 0x1809}, {"en-ZA", 0x1C09}, {"en-IN", 0x4009}, {"en-SG", 0x4809}, {"es-ES", 0x0C0A},
			{"es-MX", 0x080A}, {"es-AR", 0x2C0A}, {"es-CO", 0x240A}, {"es-CL", 0x340A}, {"es-US", 0x540A},
			{"et-EE", 0x0425}, {"eu-ES", 0x042D}, {"fa-IR", 0x0429}, {"fi-FI", 0x040B}, {"fil-PH", 0x0464},
			{"fo-FO", 0x0438}, {"fr-FR", 0x040C}, {"fr-BE", 0x080C}, {"fr-CA", 0x0C0C}, {"fr-CH", 0x100C},
			{"fr-LU", 0x140C}, {"fy-NL", 0x0462}, {"ga-IE", 0x083C}, {"gd-GB", 0x0491}, {"gl-ES", 0x0456},
			{"gu-IN", 0x0447}, {"he-IL", 0x040D}, {"hi-IN", 0x0439}, {"hr-HR", 0x041A}, {"hsb-DE", 0x042E},
			{"hu-HU", 0x040E}, {"hy-AM", 0x042B}, {"id-ID", 0x0421}, {"is-IS", 0x040F}, {"it-IT", 0x0410},
			{"it-CH", 0x0810}, {"ja-JP", 0x0411}, {"ka-GE", 0x0437}, {"kk-KZ", 0x043F}, {"kn-IN", 0x044B},
			{"ko-KR", 0x0412}, {"ky-KG", 0x0440}, {"lb-LU", 0x046E}, {"lt-LT", 0x0427}, {"lv-LV", 0x0426},
			{"mk-MK", 0x042F}, {"ml-IN", 0x044C}, {"mn-MN", 0x0450}, {"mr-IN", 0x044E}, {"ms-MY", 0x043E},
			{"mt-MT", 0x043A}, {"nb-NO", 0x0414}, {"ne-NP", 0x0461}, {"nl-NL", 0x0413}, {"nl-BE", 0x0813},
			{"nn-NO", 0x0814}, {"oc-FR", 0x0482}, {"pa-IN", 0x0446}, {"pl-PL", 0x0415}, {"ps-AF", 0x0463},
			{"pt-BR", 0x0416}, {"pt-PT", 0x0816}, {"ro-RO", 0x0418}, {"ru-RU", 0x0419}, {"sa-IN", 0x044F},
			{"si-LK", 0x045B}, {"sk-SK", 0x041B}, {"sl-SI", 0x0424}, {"sq-AL", 0x041C}, {"sr-Latn-RS", 0x241A},
			{"sr-Cyrl-RS", 0x281A}, {"sv-SE", 0x041D}, {"sv-FI", 0x081D}, {"sw-KE", 0x0441}, {"ta-IN", 0x0449},
			{"te-IN", 0x044A}, {"tg-Cyrl-TJ", 0x0428}, {"th-TH", 0x041E}, {"tk-TM", 0x0442}, {"tr-TR", 0x041F},
			{"tt-RU", 0x0444}, {"ug-CN", 0x0480}, {"uk-UA", 0x0422}, {"ur-PK", 0x0420}, {"uz-Latn-UZ", 0x0443},
			{"vi-VN", 0x042A}, {"xh-ZA", 0x0434}, {"zh-CN", 0x0804}, {"zh-TW", 0x0404}, {"zh-HK", 0x0C04},
			{"zh-SG", 0x1004}, {"zh-MO", 0x1404}, {"zu-ZA", 0x0435},
		};

		bool sameName(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// A PCI vendor's or device's name from the system's pci.ids, or the hexadecimal identifiers.
		std::string pciName(const std::string& vendor, const std::string& device)
		{
			const auto hex = [](std::string id) {
				if (id.rfind("0x", 0) == 0)
					id = id.substr(2);
				std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return id;
			};
			const std::string vendorId = hex(vendor);
			const std::string deviceId = hex(device);
			for (const char* database : {"/usr/share/misc/pci.ids", "/usr/share/hwdata/pci.ids"})
			{
				std::ifstream file(database);
				if (!file)
					continue;
				std::string vendorName;
				for (std::string line; std::getline(file, line);)
				{
					if (line.empty() || line[0] == '#')
						continue;
					if (line[0] != '\t')
					{
						if (!vendorName.empty())
							break; // past the vendor's devices
						if (line.compare(0, 4, vendorId) == 0)
							vendorName = trim(line.substr(4));
					}
					else if (!vendorName.empty() && line.size() > 6 && line[1] != '\t' && line.compare(1, 4, deviceId) == 0)
						return trim(line.substr(5));
				}
				if (!vendorName.empty())
					return vendorName + " " + deviceId;
			}
			return vendorId + ":" + deviceId;
		}
	}

	std::string preferredLanguage()
	{
		const QStringList languages = QLocale::system().uiLanguages(QLocale::TagSeparator::Dash);
		return languages.isEmpty() ? std::string{"en-US"} : toString(languages.first());
	}

	bool isLoadableModule(const void* image)
	{
#if defined(__x86_64__)
		constexpr Elf64_Half hostMachine = EM_X86_64;
#elif defined(__aarch64__)
		constexpr Elf64_Half hostMachine = EM_AARCH64;
#else
#error "The Linux build is for x86_64 or arm64."
#endif
		Elf64_Ehdr header{};
		std::memcpy(&header, image, sizeof(header));
		return std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64
			&& header.e_type == ET_DYN && header.e_machine == hostMachine;
	}

	char** environment()
	{
		return environ;
	}

	std::vector<int> processIds()
	{
		std::vector<int> pids;
		if (DIR* proc = opendir("/proc"))
		{
			while (const dirent* entry = readdir(proc))
			{
				char* end = nullptr;
				const long pid = std::strtol(entry->d_name, &end, 10);
				if (*end == '\0' && pid > 0)
					pids.push_back(static_cast<int>(pid));
			}
			closedir(proc);
		}
		return pids;
	}

	std::string processPath(int pid)
	{
		char path[4096];
		const ssize_t length = readlink(("/proc/" + std::to_string(pid) + "/exe").c_str(), path, sizeof(path) - 1);
		return length > 0 ? std::string{path, static_cast<size_t>(length)} : std::string{};
	}

	size_t allocationSize(const void* memory)
	{
		return malloc_usable_size(const_cast<void*>(memory));
	}

	bool copyFile(const std::string& source, const std::string& target, bool move)
	{
		const int from = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
		if (from < 0)
			return false;
		struct stat info{};
		if (::fstat(from, &info) != 0)
		{
			const int error = errno;
			::close(from);
			errno = error;
			return false;
		}
		const int to = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, info.st_mode & 07777);
		if (to < 0)
		{
			const int error = errno;
			::close(from);
			errno = error;
			return false;
		}
		bool copied = true;
		for (;;)
		{
			// The kernel copies (and may share blocks) without the data passing through here.
			const ssize_t count = ::copy_file_range(from, nullptr, to, nullptr, 1 << 30, 0);
			if (count == 0)
				break;
			if (count > 0)
				continue;
			if (errno != EXDEV && errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
			{
				copied = false;
				break;
			}
			char buffer[64 * 1024];
			for (ssize_t read; (read = ::read(from, buffer, sizeof(buffer))) != 0;)
			{
				if (read < 0 || ::write(to, buffer, static_cast<size_t>(read)) != read)
				{
					copied = false;
					break;
				}
			}
			break;
		}
		const int error = errno;
		if (copied)
		{
			::fchmod(to, info.st_mode & 07777);
			const timespec times[2] = {info.st_atim, info.st_mtim};
			::futimens(to, times);
		}
		::close(from);
		if (::close(to) != 0 && copied)
			return false;
		if (!copied)
		{
			errno = error;
			return false;
		}
		return !move || ::unlink(source.c_str()) == 0;
	}

	std::string machineGuid()
	{
		// systemd's machine ID: 32 hexadecimal digits, a GUID's 128 bits.
		const std::string id = trim(readFile("/etc/machine-id"));
		if (id.size() != 32)
			return {};
		return id.substr(0, 8) + "-" + id.substr(8, 4) + "-" + id.substr(12, 4) + "-" + id.substr(16, 4) + "-" + id.substr(20);
	}

	namespace
	{
		QCryptographicHash::Algorithm qtAlgorithm(DigestAlgorithm algorithm)
		{
			switch (algorithm)
			{
				case DigestAlgorithm::md5:
					return QCryptographicHash::Md5;
				case DigestAlgorithm::sha1:
					return QCryptographicHash::Sha1;
				case DigestAlgorithm::sha256:
					return QCryptographicHash::Sha256;
				case DigestAlgorithm::sha384:
					return QCryptographicHash::Sha384;
				case DigestAlgorithm::sha512:
					return QCryptographicHash::Sha512;
			}
			return QCryptographicHash::Sha256;
		}
	}

	size_t digestLength(DigestAlgorithm algorithm)
	{
		return static_cast<size_t>(QCryptographicHash::hashLength(qtAlgorithm(algorithm)));
	}

	std::vector<unsigned char> digest(DigestAlgorithm algorithm, const void* data, size_t size)
	{
		const QByteArray result = QCryptographicHash::hash(
			QByteArrayView(static_cast<const char*>(data), static_cast<qsizetype>(size)), qtAlgorithm(algorithm));
		return {result.begin(), result.end()};
	}

	bool memoryRegion(uintptr_t address, MemoryRegion& region)
	{
		// /proc/self/maps lists the mappings in address order: "start-end perms offset dev inode path".
		std::ifstream maps("/proc/self/maps");
		for (std::string line; std::getline(maps, line);)
		{
			unsigned long long start = 0;
			unsigned long long end = 0;
			char permissions[5]{};
			if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) != 3 || end <= address)
				continue;
			region._start = static_cast<uintptr_t>(start);
			region._size = static_cast<uintptr_t>(end - start);
			region._readable = permissions[0] == 'r';
			region._writable = permissions[1] == 'w';
			region._executable = permissions[2] == 'x';
			return true;
		}
		return false;
	}

	unsigned windowsLocaleCode(const std::string& name)
	{
		std::string wanted = name;
		std::replace(wanted.begin(), wanted.end(), '_', '-');
		if (const size_t dot = wanted.find_first_of(".@"); dot != std::string::npos)
			wanted.resize(dot); // en-US.UTF-8, sr-RS@latin
		if (sameName(wanted, "zh-Hans") || sameName(wanted, "zh-Hans-CN"))
			wanted = "zh-CN";
		else if (sameName(wanted, "zh-Hant") || sameName(wanted, "zh-Hant-TW"))
			wanted = "zh-TW";
		for (const LocaleCode& code : localeCodes)
		{
			if (sameName(wanted, code._name))
				return code._lcid;
		}
		// A language alone ("fr"), or with a region not listed: its default.
		const std::string language = wanted.substr(0, wanted.find('-'));
		for (const LocaleCode& code : localeCodes)
		{
			const std::string_view codeName = code._name;
			if (sameName(language, codeName.substr(0, codeName.find('-'))))
				return code._lcid;
		}
		return 0;
	}

	void activateApplication()
	{
		// With J4's window step. A test run never takes the keyboard.
	}

	void registerApplication()
	{
		// The desktop's registration (a .desktop file and the MIME types) comes with J4's desktop step.
	}

	bool isDarkAppearance()
	{
		if (const char* test = std::getenv("NPPMAC_TEST_APPEARANCE"))
			return std::strcmp(test, "dark") == 0;
		if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
			return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
		return false;
	}

	unsigned accentColor()
	{
		if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
			return QGuiApplication::palette().color(QPalette::Accent).rgb() & 0xFFFFFF;
		return 0x0078D7;
	}

	std::vector<std::string> graphicsAdapters()
	{
		std::vector<std::string> names;
		QDir drm("/sys/class/drm");
		for (const QString& card : drm.entryList({"card*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		{
			if (card.contains('-'))
				continue; // a connector (card0-eDP-1), not a device
			const std::string device = "/sys/class/drm/" + toString(card) + "/device/";
			const std::string vendorId = trim(readFile(device + "vendor"));
			const std::string deviceId = trim(readFile(device + "device"));
			if (!vendorId.empty() && !deviceId.empty())
				names.push_back(pciName(vendorId, deviceId));
		}
		return names;
	}

	std::string systemName()
	{
		const std::string name = osRelease("NAME");
		return name.empty() ? std::string{"Linux"} : name;
	}

	std::string systemVersion()
	{
		const std::string version = osRelease("VERSION");
		return version.empty() ? osRelease("VERSION_ID") : version;
	}

	std::string systemBuild()
	{
		utsname system{};
		return uname(&system) == 0 ? std::string{system.release} : std::string{};
	}

	std::string executablePath()
	{
		return processPath(static_cast<int>(getpid()));
	}

	std::string resourcesDirectory()
	{
		// The program's own folder, where Notepad++ finds its files beside notepad++.exe. J4's desktop
		// step decides the installed layout.
		const std::string path = executablePath();
		return path.substr(0, path.find_last_of('/'));
	}

	std::string applicationSupportDirectory()
	{
		// Test runs keep what is stored per user (plugins' settings) in a folder of their own.
		if (const char* testFolder = std::getenv("NPPMAC_TEST_APPDATA"); testFolder && *testFolder)
			return testFolder;
		// What Windows keeps in %APPDATA% belongs in the XDG configuration folder (~/.config).
		return toString(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
	}

	std::string documentsDirectory()
	{
		return toString(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	}

	std::string desktopDirectory()
	{
		return toString(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
	}

	std::string homeDirectory()
	{
		return toString(QDir::homePath());
	}

	std::string userName()
	{
		const passwd* user = account();
		return user ? std::string{user->pw_name} : std::string{};
	}

	std::string fullUserName()
	{
		// The GECOS field's first part: "Jane Appleseed,Room,Phone..."
		const passwd* user = account();
		std::string name = user && user->pw_gecos ? std::string{user->pw_gecos} : std::string{};
		name = name.substr(0, name.find(','));
		return name.empty() ? userName() : name;
	}

	std::u16string normalize(std::u16string_view text, Normalization form)
	{
		const QString string = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
		QString::NormalizationForm qtForm = QString::NormalizationForm_C;
		switch (form)
		{
			case Normalization::composed:
				qtForm = QString::NormalizationForm_C;
				break;
			case Normalization::decomposed:
				qtForm = QString::NormalizationForm_D;
				break;
			case Normalization::compatibilityComposed:
				qtForm = QString::NormalizationForm_KC;
				break;
			case Normalization::compatibilityDecomposed:
				qtForm = QString::NormalizationForm_KD;
				break;
		}
		const QString normalized = string.normalized(qtForm);
		return std::u16string{reinterpret_cast<const char16_t*>(normalized.utf16()), static_cast<size_t>(normalized.size())};
	}

	std::string computerName()
	{
		// The name the user gave it (hostnamectl's pretty name), else the host name.
		std::istringstream lines(readFile("/etc/machine-info"));
		for (std::string line; std::getline(lines, line);)
		{
			if (line.rfind("PRETTY_HOSTNAME=", 0) == 0)
			{
				std::string name = line.substr(16);
				if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
					name = name.substr(1, name.size() - 2);
				if (!name.empty())
					return name;
			}
		}
		char host[256]{};
		return gethostname(host, sizeof(host) - 1) == 0 ? std::string{host} : std::string{"Linux"};
	}

	bool openUrlOrFile(const std::string& urlOrPath, const std::string& applicationPath)
	{
		if (!applicationPath.empty())
			return QProcess::startDetached(toQString(applicationPath), {toQString(urlOrPath)});
		// xdg-open asks the desktop for the default application, without needing this process's windows.
		return QProcess::startDetached("xdg-open", {toQString(urlOrPath)});
	}

	bool revealInFinder(const std::vector<std::string>& paths)
	{
		// The file manager's freedesktop.org interface selects the items in their folder.
		QStringList uris;
		for (const std::string& path : paths)
			uris << QUrl::fromLocalFile(toQString(path)).toString();
		QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
			"org.freedesktop.FileManager1", "ShowItems");
		call << uris << QString{};
		if (QDBusConnection::sessionBus().call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage)
			return true;
		// Without one, the first item's folder.
		return !paths.empty() && openUrlOrFile(toString(QFileInfo(toQString(paths.front())).absolutePath()));
	}

	bool moveToTrash(const std::string& path)
	{
		return QFile::moveToTrash(toQString(path));
	}

	void beep()
	{
		// With J4's dialogs step (the desktop's alert sound).
	}

	long pasteboardChangeCount()
	{
		return 0; // with J4's dialogs step
	}

	std::vector<std::string> pasteboardTypes()
	{
		return {}; // with J4's dialogs step
	}

	std::vector<unsigned char> pasteboardData(const std::string&)
	{
		return {}; // with J4's dialogs step
	}

	void setPasteboard(const std::vector<std::pair<std::string, std::vector<unsigned char>>>&)
	{
		// with J4's dialogs step
	}

	FilePanelResult runFilePanel(const FilePanelRequest& request)
	{
		FilePanelResult result;
		result._fileTypeIndex = request._fileTypeIndex;
		result._checkBoxes = request._checkBoxes;
		if (const char* test = std::getenv("NPPMAC_TEST_FILE_PANEL"))
		{
			result._ok = *test != '\0';
			if (result._ok)
				result._paths.push_back(test);
		}
		return result; // the panel itself with J4's dialogs step
	}

	bool chooseColor(unsigned& rgb)
	{
		if (const char* test = std::getenv("NPPMAC_TEST_COLOR"))
		{
			rgb = static_cast<unsigned>(std::strtoul(test, nullptr, 16)) & 0xFFFFFF;
			return true;
		}
		return false; // the chooser with J4's dialogs step
	}

	bool chooseFont(FontChoice& choice)
	{
		if (const char* test = std::getenv("NPPMAC_TEST_FONT"))
		{
			std::istringstream parts(test);
			std::string part;
			std::getline(parts, choice._family, ':');
			if (std::getline(parts, part, ':'))
				choice._size = std::strtod(part.c_str(), nullptr);
			choice._bold = false;
			choice._italic = false;
			while (std::getline(parts, part, ':'))
			{
				choice._bold = choice._bold || part == "bold";
				choice._italic = choice._italic || part == "italic";
			}
			return true;
		}
		return false; // the chooser with J4's dialogs step
	}

	bool choosePrinter(PrintSetup&, bool)
	{
		return std::getenv("NPPMAC_TEST_PRINT_PDF") != nullptr; // the printer with J4's dialogs step
	}

	bool printPdf(const std::string& pdfPath, const std::string&)
	{
		if (const char* test = std::getenv("NPPMAC_TEST_PRINT_PDF"))
		{
			QFile::remove(test);
			return QFile::copy(toQString(pdfPath), test);
		}
		return false; // with J4's dialogs step
	}

	int showAlert(const std::string&, const std::string&, const std::vector<std::string>&, AlertStyle, int defaultButton, int)
	{
		return defaultButton; // the alert with J4's dialogs step
	}
}
