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

nppmac_add_external_plugin(BigFiles
	DISPLAY_NAME "BigFiles - Open Very Large Files"
	VERSION 0.1.3
	GIT https://github.com/superolmo/BigFiles.git
	COMMIT 3196c9eab5815f83f863de6882ee803b42736ae3
	SOURCES src/BigFiles.cpp src/Configuration.cpp src/FileTracker.cpp src/libmagic_alike.cpp src/PluginDefinition.cpp src/Support.cpp src/v3.cpp
	RC_FILES src/BigFiles.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(BlitzSearch
	DISPLAY_NAME "Blitz Search"
	VERSION 1.0.0.6
	GIT https://github.com/Natestah/BlitsNppPlugin.git
	COMMIT d91e43906182ecd4cb809ccdf9de549021bb3382
	SOURCES src/BlitzSearch.cpp src/PluginDefinition.cpp
	RC_FILES src/BlitzSearch.rc
	INCLUDES src/vs src/npp
	DEFINES NPPPLUGINDEMO_EXPORTS)

nppmac_add_external_plugin(CommentToggler
	DISPLAY_NAME "Comment Toggler"
	VERSION 1.0.0.0
	GIT https://github.com/ScienceDiscoverer/CommentToggler.git
	COMMIT bbae39d806b91fa8ef5e63d3210eb76c6ee919ce
	SOURCES CommentToggler.cpp PluginDefinition.cpp
	RC_FILES CommentToggler.rc
	INCLUDES vs npp
	DEFINES NPPPLUGINDEMO_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(FoldingLineHider
	DISPLAY_NAME "Folding Line Hider"
	VERSION 1.1
	GIT https://github.com/leonardchai/FoldingLineHider.git
	COMMIT 0c43f177027ba7747d5b30fd1322eb469273e90c
	SOURCES src/NppPlugin.cpp src/PluginDefinition.cpp
	RC_FILES src/NppPlugin.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGIN_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(IndentByFold
	DISPLAY_NAME "Indent By Fold"
	VERSION 0.7.3
	GIT https://github.com/ffes/indentbyfold.git
	COMMIT 2ebc630a63d47144b878d948cd1f89f5ba7d9126
	SOURCES IBFMenu.cpp IBFPlugin.cpp IndentByFold.cpp NppMessager.cpp NppPlugin.cpp NppPluginMenu.cpp SciMessager.cpp WaitCursor.cpp
	RC_FILES IndentByFold_res.rc
	INCLUDES .
	DEFINES "_WIN32_WINNT= 0x501")

nppmac_add_external_plugin(NppRgaIsaPlugin
	DISPLAY_NAME "RGA ISA Compiler"
	VERSION 1.0.0.0
	GIT https://github.com/DIMOSUS/NppRgaIsaPlugin.git
	COMMIT 2ffce47771a6bd0070c6fb84c891976f0d3f11d4
	SOURCES src/DockingFeature/isaDlg.cpp src/DockingFeature/StaticDialog.cpp src/NppIasPlugin.cpp src/PluginDefinition.cpp
	RC_FILES src/DockingFeature/isaPanel.rc src/NppIasPlugin.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(SecurePad
	DISPLAY_NAME SecurePad
	VERSION 2.4
	GIT https://github.com/DominicTobias/SecurePad.git
	COMMIT 12cb5a0ec46b2536595e2af642487ad87fc2f212
	SOURCES blowfish.cpp NppPluginDemo.cpp PluginDefinition.cpp
	RC_FILES Resources.rc
	INCLUDES .
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NppTaskList
	DISPLAY_NAME "Task List"
	VERSION 2.7
	GIT https://github.com/Megabyteceer/npp-task-list.git
	COMMIT 7d53ea90d34b099cbd45d574ad3f3dcfa2938874
	SOURCES src/AboutDialog/AboutDlg.cpp src/Common.cpp src/config.cpp src/dpiManagerV2.cpp src/NppDarkModeDummy.cpp src/NppTaskListPlugin.cpp src/PluginDefinition.cpp src/StaticDialog.cpp src/TaskListDlg.cpp src/URLCtrl.cpp
	RC_FILES resources/TaskList.rc
	INCLUDES . include
	DEFINES _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING NPPPLUGINTASKLIST_EXPORTS _CRT_NONSTDC_NO_DEPRECATE)

nppmac_add_external_plugin(pork2sausage
	DISPLAY_NAME "Pork to Sausage"
	VERSION 2.6
	GIT https://github.com/npp-plugins/pork2sausage.git
	COMMIT cce79843879d66469f8c5d6459e2f49f11e6fa53
	SOURCES src/Pork2Sausage.cpp src/Process.cpp
	RC_FILES src/Pork2Sausage.rc
	INCLUDES vs.proj
	DEFINES PORK2SAUSAGE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)
