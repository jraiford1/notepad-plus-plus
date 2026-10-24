# The plugins of the official plugin list built for macOS (see README.md, Plugins), each from its own
# sources at a pinned commit. Entries are written by import-plugins.py from the plugin's Visual Studio
# project; a plugin is listed once it builds.

nppmac_add_external_plugin(nppAutoDetectIndent
	DISPLAY_NAME "Auto Detect Indention"
	VERSION 2.3
	GIT https://github.com/Chocobo1/nppAutoDetectIndent.git
	COMMIT 1c8af156d330d169c406e5fa7aae133830961aff
	SOURCES src/dllmain.cpp src/nppAutoDetectIndent.cpp src/settings.cpp src/stdafx.cpp
	RC_FILES src/nppAutoDetectIndent.rc
	INCLUDES src
	DEFINES NPPAUTODETECTINDENT_EXPORTS)

nppmac_add_external_plugin(BetterMultiSelection
	DISPLAY_NAME BetterMultiSelection
	VERSION 1.5
	GIT https://github.com/dail8859/BetterMultiSelection.git
	COMMIT 4c63c6dd8eaf4f2df5a56c87ab79a57bb9c671c4
	SOURCES src/Dialogs/AboutDialog.cpp src/Dialogs/Hyperlinks.cpp src/Main.cpp src/UniConversion.cpp
	RC_FILES src/Dialogs/resource.rc src/Dialogs/Version.rc
	INCLUDES src src/Dialogs src/Parsers src/Npp src/Utilities
	DEFINES __STDC_WANT_SECURE_LIB__=1 _CRT_NONSTDC_NO_DEPRECATE)

nppmac_add_external_plugin(dark_mode_C
	DISPLAY_NAME "Dark Mode C"
	VERSION 1.0.3
	GIT https://github.com/pullitall/Notepadpp-light-dark-switch-and-automatic-switch-based-on-location.git
	COMMIT 95217e255cb6dbb53a889b2ce77cbf717643a4c0
	SOURCES dark_mode_C/dark_mode_C/dllmain.cpp dark_mode_C/dark_mode_C/pch.cpp dark_mode_C/dark_mode_C/PluginDefinition.cpp dark_mode_C/dark_mode_C/PluginCommands.cpp
	RC_FILES dark_mode_C/dark_mode_C/resources.rc
	INCLUDES dark_mode_C/dark_mode_C
	DEFINES DARKMODEC_EXPORTS)

nppmac_add_external_plugin(DualScrollbars
	DISPLAY_NAME "Dual Scrollbars"
	VERSION 1.1.0
	GIT https://github.com/SaiyajinK/Dual-Scrollbars-for-Notepad-Plus-Plus.git
	COMMIT dac6573a02947a502eb2773a80bfd583d3a9aca5
	SOURCES src/DualScrollbars.cpp
	RC_FILES src/DualScrollbarsVersion.rc
	INCLUDES .
	DEFINES UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

nppmac_add_external_plugin(Language_Selector
	DISPLAY_NAME "Language Selector"
	VERSION 1.2.0.0
	GIT https://github.com/hassaine-abdellah/npp-language-selector.git
	COMMIT 405c231bea92b0d141e2fde1843265d3d0875963
	SOURCES src/PluginDefinition.cpp
	RC_FILES src/Language_Selector.rc src/resource.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(mimeTools
	DISPLAY_NAME "Mime tools"
	VERSION 3.1
	GIT https://github.com/npp-plugins/mimetools.git
	COMMIT 1add230a2660e09029460c89f4578d3083bc4085
	SOURCES src/b64.cpp src/mimeTools.cpp src/qp.cpp src/saml.cpp src/tinflate.c src/url.cpp
	RC_FILES src/mimeTools.rc
	INCLUDES vs.proj
	DEFINES MIMETOOLS_EXPORTS)

nppmac_add_external_plugin(NppPluginDemo
	DISPLAY_NAME "Notepad++ Plugin Demo"
	VERSION 4.5
	GIT https://github.com/npp-plugins/plugindemo.git
	COMMIT 030d76485ebc5d3e94cbb2019472319db72ad9ae
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/NppPluginDemo.cpp src/PluginDefinition.cpp
	RC_FILES src/DockingFeature/goLine.rc src/NppPluginDemo.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINDEMO_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NppPluginTemplate
	DISPLAY_NAME "Notepad++ Plugin Template"
	VERSION 4.4
	GIT https://github.com/npp-plugins/plugintemplate.git
	COMMIT abd8c2394936e934753db08ab1684aae1dcf7fd4
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/NppPluginDemo.cpp src/PluginDefinition.cpp
	RC_FILES src/DockingFeature/goLine.rc src/NppPluginDemo.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(nppConverter
	DISPLAY_NAME "Npp Converter"
	VERSION 4.7
	GIT https://github.com/npp-plugins/converter.git
	COMMIT dbba091853eb27580ea8418bba1c5a907885d211
	SOURCES source/conversionPanel.cpp source/DockingFeature/StaticDialog.cpp source/NppPluginDemo.cpp source/PluginDefinition.cpp
	RC_FILES source/conversionPanel.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINCONVERTER_EXPORTS _CRT_NONSTDC_NO_DEPRECATE)

nppmac_add_external_plugin(NppCrossCheck
	DISPLAY_NAME NppCrossCheck
	VERSION 1.3.0.0
	GIT https://github.com/pablo-code14/NppCrossCheck.git
	COMMIT 6cbe0a04b000160ba9a3c135fd65cbc2ec42fddb
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/NppCrossCheck.cpp src/PluginDefinition.cpp
	RC_FILES src/DockingFeature/goLine.rc src/NppCrossCheck.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NppRestoreCursorPosition
	DISPLAY_NAME NppRestoreCursorPosition
	VERSION 1.0.0
	GIT https://github.com/RivieraSystems/NppRestoreCursorPosition.git
	COMMIT 4f84cf3ad4c6b588d268632dfe1cdb96b4e588f5
	SOURCES src/NppRestoreCursorPosition.cpp src/PluginDefinition.cpp src/HistoryStore.cpp src/UiText.cpp
	RC_FILES NppRestoreCursorPosition.rc
	INCLUDES . src npp
	DEFINES NPPRESTORECURSORPOSITION_EXPORTS UNICODE _UNICODE)

nppmac_add_external_plugin(NppStripIpAndHost
	DISPLAY_NAME NppStripIpAndHost
	VERSION 1.0.0
	GIT https://github.com/larreblarsson/NppStripIpAndHost.git
	COMMIT 589e6f53f3c934f1acc949bd701855b0b33527f9
	SOURCES src/NppPluginDemo.cpp src/PluginDefinition.cpp
	RC_FILES src/NppPluginDemo.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NppZenHankaku
	DISPLAY_NAME NppZenHankaku
	VERSION 1.0.0
	GIT https://github.com/RivieraSystems/NppZenHankaku.git
	COMMIT ca56e00034a2427c329651c67aff75375768d90b
	SOURCES src/NppZenHankaku.cpp src/PluginDefinition.cpp src/converter.cpp src/DbcsCodec.cpp src/ToolbarIcons.cpp
	RC_FILES NppZenHankaku.rc
	INCLUDES . src npp
	DEFINES NPPZENHANKAKU_EXPORTS UNICODE _UNICODE)

nppmac_add_external_plugin(qkNppReverseLines
	DISPLAY_NAME "Reverse Lines"
	VERSION 1.0.0.0
	GIT https://github.com/querykuma/qkNppReverseLines.git
	COMMIT c15eb32e465f2b1ce8d7b025511140af6789d025
	SOURCES src/NppReverseLines.cpp src/PluginDefinition.cpp
	RC_FILES src/NppReverseLines.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(ScrollMarks
	DISPLAY_NAME ScrollMarks
	VERSION 1.1.0
	GIT https://github.com/Hong-Seungmin/ScrollMarks.git
	COMMIT 96d1e4675cdc8e3a5bc93500e15a0beb92f83b79
	SOURCES src/MarkerBar.cpp src/MarkLayers.cpp src/MarkScan.cpp src/NppPreferences.cpp src/OccurrenceSearch.cpp src/Plugin.cpp src/PluginMain.cpp src/PreviewWindow.cpp src/Settings.cpp src/SettingsDialog.cpp src/Strings.cpp
	RC_FILES src/ScrollMarks.rc
	INCLUDES . npp src
	DEFINES WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

nppmac_add_external_plugin(selectNLaunch
	DISPLAY_NAME "Select N' Launch"
	VERSION 2.2
	GIT https://github.com/npp-plugins/selectnlaunch.git
	COMMIT d124bb18404fdd218c453cbde5f1c8fe3bc8456b
	SOURCES src/b64.cpp src/SelectNLaunch.cpp
	RC_FILES src/SelectNLaunch.rc
	INCLUDES vs.proj
	DEFINES SELECTNLAUNCH_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(SurroundSelection
	DISPLAY_NAME SurroundSelection
	VERSION 1.4.1
	GIT https://github.com/dail8859/SurroundSelection.git
	COMMIT 916570e9d4bcbc9d7b2046508c6ce685744b220a
	SOURCES src/Dialogs/AboutDialog.cpp src/Dialogs/Hyperlinks.cpp src/Main.cpp
	RC_FILES src/Dialogs/resource.rc src/Dialogs/Version.rc
	INCLUDES src src/Dialogs src/Parsers src/Npp src/Utilities
	DEFINES __STDC_WANT_SECURE_LIB__=1 _CRT_NONSTDC_NO_DEPRECATE)

nppmac_add_external_plugin(TCSyslogFinder
	DISPLAY_NAME "TC Syslog Finder"
	VERSION 1.1.0
	GIT https://github.com/bsagarzazu/npp-tc-syslog-finder.git
	COMMIT db543160aa4b33572d373d1e588d79603c7bd1c0
	SOURCES src/TCSyslogFinder.cpp src/PluginDefinition.cpp
	RC_FILES src/TCSyslogFinder.rc
	INCLUDES vs.proj
	DEFINES TCSyslogFinder_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(TidyText
	DISPLAY_NAME TidyText
	VERSION 1.2.0.0
	GIT https://github.com/gonzalu/TidyText.git
	COMMIT ab3809e132a4ab0b479fb8a42d509b1a16b75889
	SOURCES src/TidyText.cpp src/PluginDefinition.cpp src/NonPrintingChars.cpp src/SendToAI.cpp src/ContextMenu.cpp
	RC_FILES src/TidyText.rc
	INCLUDES vs.proj
	DEFINES TIDYTEXT_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)
