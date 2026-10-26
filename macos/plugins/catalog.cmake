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

nppmac_add_external_plugin(ColumnTools
	DISPLAY_NAME ColumnTools
	VERSION 1.4.5.1
	GIT https://github.com/vinsworldcom/nppColumnTools.git
	COMMIT e7c4d6fe2dac40edd560ad1300a398666ef6c904
	SOURCES ColumnTools.cpp PluginDefinition.cpp HorizontalRuler.cpp NppHorizontalRuler.cpp Ini.cpp SettingsDlg.cpp
	RC_FILES ColumnTools.rc
	INCLUDES .
	DEFINES NppGitSCM_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(DoxyIt
	DISPLAY_NAME DoxyIt
	VERSION 0.4.4
	GIT https://github.com/dail8859/DoxyIt.git
	COMMIT 8c251ad6ee8eef6aef2d5b4cad73017c42a87450
	SOURCES src/Dialogs/AboutDialog.cpp src/Dialogs/Hyperlinks.cpp src/Dialogs/SettingsDialog.cpp src/Dialogs/StaticDialog.cpp src/DoxyIt.cpp src/JumpLocations.cpp src/Parsers/C_Parser.cpp src/Parsers/Null_Parser.cpp src/Parsers/Parsers.cpp src/Parsers/Python_Parser.cpp src/PluginDefinition.cpp src/Utilities/trex.cpp src/Utilities/Utils.cpp
	RC_FILES src/Dialogs/resource.rc src/Dialogs/Version.rc
	INCLUDES src src/Dialogs src/Parsers src/Npp src/Utilities
	DEFINES __STDC_WANT_SECURE_LIB__=1 _CRT_NONSTDC_NO_DEPRECATE)

nppmac_add_external_plugin(EmojiDescription
	DISPLAY_NAME "Emoji Description"
	VERSION 0.4.17
	GIT https://github.com/Ruberoid/npp_emoji_description.git
	COMMIT 96229c12bbd60f754fc5acae18dd788333770467
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/CharDetailsDialog.cpp src/EmojiDescription.cpp src/PluginDefinition.cpp src/UnicodeNames.cpp
	RC_FILES src/DockingFeature/goLine.rc src/EmojiDescription.rc
	INCLUDES vs.proj
	DEFINES EMOJIDESCRIPTION_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(GitSCM
	DISPLAY_NAME GitSCM
	VERSION 1.4.10.1
	GIT https://github.com/vinsworldcom/nppGitSCM.git
	COMMIT 5210b315d0cdad7de18fb3ec855dfd39dc604489
	SOURCES DockingFeature/ContextMenu.cpp DockingFeature/GitPanelDlg.cpp DockingFeature/Process.cpp DockingFeature/SettingsDlg.cpp DockingFeature/StaticDialog.cpp GitSCM.cpp PluginDefinition.cpp
	RC_FILES DockingFeature/gitPanel.rc GitSCM.rc
	INCLUDES .
	DEFINES GitSCM_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(GotoLineCol
	DISPLAY_NAME GotoLineCol
	VERSION 2.4.5.1
	GIT https://github.com/shriprem/Goto-Line-Col-NPP-Plugin.git
	COMMIT f15785cb6f59b0bf8c98515724e90beafb4ec522
	SOURCES src/CommandLineOptions.cpp src/Darkmode/DarkMode.cpp src/Darkmode/NppDarkMode.cpp src/Darkmode/NPP_Plugin_Darkmode.cpp src/Dialogs/AboutDialog.cpp src/Dialogs/GoToLineColPanel.cpp src/Dialogs/PreferencesDialog.cpp src/NameBlocks.cpp src/NPP/dpiManagerV2.cpp src/NPP/StaticDialog.cpp src/GotoLineColMain.cpp src/PluginDefinition.cpp src/PreferencesIO.cpp src/Utils.cpp
	RC_FILES src/Resources/AboutUI.rc src/Resources/GotoLineColPlugin.rc src/Resources/GoToLineColUI.rc
	INCLUDES vs.proj
	DEFINES _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(MarkdownTableEditor
	DISPLAY_NAME "Markdown Table Editor"
	VERSION 11.2.0
	GIT https://github.com/krotname/NppMarkdownTableEditor.git
	COMMIT a1ed08ed8124b5793214058ac2ae743131c9faa4
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/MarkdownTableCore.cpp src/MarkdownTableEditorPlugin.cpp src/PluginDefinition.cpp
	RC_FILES src/DockingFeature/goLine.rc src/MarkdownTableEditor.rc
	INCLUDES vs.proj
	DEFINES MARKDOWNTABLEEDITOR_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1
	RC_DEFINES MARKDOWN_TABLE_EDITOR_VERSION_MAJOR=11 MARKDOWN_TABLE_EDITOR_VERSION_MINOR=2 MARKDOWN_TABLE_EDITOR_VERSION_PATCH=2 MARKDOWN_TABLE_EDITOR_VERSION_BUILD=0)

nppmac_add_external_plugin(NppExport
	DISPLAY_NAME NppExport
	VERSION 0.4.0.0
	GIT https://github.com/chcg/NPP_ExportPlugin.git
	COMMIT 174b17aefd9148c72bc857804adfe0aaf67200fc
	SOURCES src/Exporter.cpp src/HTMLExporter.cpp src/NppExport.cpp src/RTFExporter.cpp src/stdafx.cpp src/TXTExporter.cpp
	RC_FILES src/resource.rc
	INCLUDES vs7project src/Notepad_specific
	DEFINES NPPPLUGIN_EXPORTS)

nppmac_add_external_plugin(NppFavorites
	DISPLAY_NAME NppFavorites
	VERSION 1.0.0.1
	GIT https://github.com/heldersepu/nppfavorites.git
	COMMIT 6f92721f92c6193a78f6fcf5b17fbeb73445b749
	SOURCES NppFavorites.cpp PluginDefinition.cpp DockingFeature/StaticDialog.cpp
	RC_FILES version_info.rc
	INCLUDES .
	DEFINES NppFavorites_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(QuickText
	DISPLAY_NAME QuickText
	VERSION 0.2.5.1
	GIT https://github.com/vinsworldcom/nppQuickText.git
	COMMIT fc21782d3b8d7553b5df4ce7936758af489fadd4
	SOURCES lib/INIMap.cpp QuickText.cpp
	RC_FILES QuickText.rc
	INCLUDES .
	DEFINES QUICKTXT_EXPORTS _CRT_SECURE_NO_DEPRECATE
	FILES Config/QuickText.default.ini Config/QuickText.conf.default.ini)

nppmac_add_external_plugin(SelectQuotedText
	DISPLAY_NAME SelectQuotedText
	VERSION 1.1.0
	GIT https://github.com/ffes/selectquotedtext.git
	COMMIT 67c1f6098fc31bc91d75d0031c71ae1afa9f9b7e
	SOURCES DlgAbout.cpp NppMessenger.cpp SelectQuotedText.cpp
	RC_FILES SelectQuotedText_res.rc
	INCLUDES .
	DEFINES SelectQuotedText_EXPORTS)

nppmac_add_external_plugin(urlPlugin
	DISPLAY_NAME "URL Encode/Decode Plugin"
	VERSION 1.2.0.0
	GIT https://github.com/SinghRajenM/nppURLPlugin.git
	COMMIT 6b835e4fa7f0ff1f34d227fe07f94b3ef8dd3b70
	SOURCES src/urlPlugin/AboutDlg.cpp src/urlPlugin/dllmain.cpp src/urlPlugin/external/npp/ControlsTab.cpp src/urlPlugin/external/npp/StaticDialog.cpp src/urlPlugin/external/npp/TabBar.cpp src/urlPlugin/external/npp/URLCtrl.cpp src/urlPlugin/PluginHelper.cpp src/urlPlugin/Profile.cpp src/urlPlugin/ScintillaEditor.cpp src/urlPlugin/SettingsDlg.cpp src/urlPlugin/ShortcutCommand.cpp src/utilityLib/Execute.cpp src/utilityLib/pch.cpp src/utilityLib/StringHelper.cpp src/utilityLib/Utility.cpp
	RC_FILES src/urlPlugin/urlPlugin.rc
	INCLUDES src/urlPlugin src/urlPlugin/external/npp src/utilityLib
	DEFINES URLPLUGIN_EXPORTS)

nppmac_add_external_plugin(XBrackets
	DISPLAY_NAME "XBrackets Lite"
	VERSION 2.0.4
	GIT https://github.com/d0vgan/npp-XBracketsLite.git
	COMMIT 76b12358e1f2cad082830b429e13033af61509b4
	SOURCES XBrackets/src/CFileModificationWatcher.cpp XBrackets/src/core/NppMessager.cpp XBrackets/src/core/NppPlugin.cpp XBrackets/src/core/NppPluginMenu.cpp XBrackets/src/core/SciMessager.cpp XBrackets/src/json11/json11.cpp XBrackets/src/Main.cpp XBrackets/src/SettingsDlg.cpp XBrackets/src/XBracketsCommon.cpp XBrackets/src/XBracketsLogic.cpp XBrackets/src/XBracketsPlugin.cpp XBrackets/src/XBracketsMenu.cpp XBrackets/src/XBracketsOptions.cpp
	RC_FILES XBrackets/src/XBrackets.rc
	INCLUDES XBrackets
	DEFINES XBRACKETS_EXPORTS
	FILES XBrackets/XBrackets_Config.json)

nppmac_add_external_plugin(FWDataViz
	DISPLAY_NAME "Fixed-width Data Visualizer"
	VERSION 2.6.7.0
	GIT https://github.com/shriprem/FWDataViz.git
	COMMIT 6b088aac6698cba953974e93032c7599d24f217d
	SOURCES src/ConfigIO.cpp src/Darkmode/DarkMode.cpp src/Darkmode/NppDarkMode.cpp src/Darkmode/NPP_Plugin_Darkmode.cpp src/Dialogs/AboutDialog.cpp src/Dialogs/ConfigureDialog.cpp src/Dialogs/DataExtractDialog.cpp src/Dialogs/EximFileTypeDialog.cpp src/Dialogs/FieldTypeDialog.cpp src/Dialogs/FoldStructDialog.cpp src/Dialogs/JumpToField.cpp src/Dialogs/PreferencesDialog.cpp src/Dialogs/StyleDefComponent.cpp src/Dialogs/ThemeDialog.cpp src/Dialogs/VisualizerPanel.cpp src/NPP/dpiManagerV2.cpp src/NPP/StaticDialog.cpp src/SubmenuManager.cpp src/Utils.cpp src/VisualizerMain.cpp src/PluginDefinition.cpp
	RC_FILES src/Resources/AboutUI.rc src/Resources/VisualizerPlugin.rc src/Resources/VisualizerUI.rc
	INCLUDES vs.proj
	DEFINES _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NestedDSV
	DISPLAY_NAME "NestedDSV Data Visualizer"
	VERSION 1.1.0.1
	GIT https://github.com/shriprem/NestedDSV.git
	COMMIT dd4eb5c2bb5c046c35fdf69c12dd67ea45e56058
	SOURCES src/ConfigIO.cpp src/Darkmode/DarkMode.cpp src/Darkmode/NppDarkMode.cpp src/Darkmode/NPP_Plugin_Darkmode.cpp src/Dialogs/AboutDialog.cpp src/Dialogs/ConfigureDialog.cpp src/Dialogs/DataExtractDialog.cpp src/Dialogs/EximFileTypeDialog.cpp src/Dialogs/FieldTypeDialog.cpp src/Dialogs/FoldStructDialog.cpp src/Dialogs/JumpToField.cpp src/Dialogs/NestedDSVPanel.cpp src/Dialogs/PreferencesDialog.cpp src/Dialogs/StyleDefComponent.cpp src/Dialogs/ThemeDialog.cpp src/NestedDSVMain.cpp src/NPP/dpiManagerV2.cpp src/NPP/StaticDialog.cpp src/PluginDefinition.cpp src/SubmenuManager.cpp src/Utils.cpp
	RC_FILES src/Resources/AboutUI.rc src/Resources/NestedDSVPlugin.rc src/Resources/NestedDSVUI.rc
	INCLUDES vs.proj
	DEFINES _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(NppSnippets
	DISPLAY_NAME Snippets
	VERSION 1.7.1
	GIT https://github.com/ffes/nppsnippets.git
	COMMIT 24248b69b45d0ddf45e04f9749b597469a376616
	SOURCES DlgAbout.cpp DlgConsole.cpp DlgEditLanguages.cpp DlgEditLibrary.cpp DlgEditSnippet.cpp DlgImportLibrary.cpp DlgOptions.cpp Language.cpp Library.cpp NppOptions.cpp NppSnippets.cpp Options.cpp Snippets.cpp SnippetsDB.cpp sqlite3.c SqliteDB.cpp WaitCursor.cpp
	RC_FILES NppSnippets_res.rc
	INCLUDES .
	DEFINES NPPSNIPPETS_EXPORTS
	FILES misc/Template.sqlite)

nppmac_add_external_plugin(NppTags
	DISPLAY_NAME NppTags
	VERSION 0.9.1
	GIT https://github.com/ffes/npptags.git
	COMMIT 08bb850c24bca62ab3e29fb991a74c41470eea93
	SOURCES DlgAbout.cpp DlgOptions.cpp DlgSelectTag.cpp DlgTree.cpp NppOptions.cpp NppTags.cpp Options.cpp readtags.c sqlite3.c SqliteDB.cpp Tag.cpp TagsDatabase.cpp TreeBuilder.cpp TreeBuilderCpp.cpp TreeBuilderCSharp.cpp TreeBuilderJava.cpp TreeBuilderRst.cpp TreeBuilderSql.cpp WaitCursor.cpp
	RC_FILES NppTags_res.rc
	INCLUDES .
	DEFINES NppTags_EXPORTS)

nppmac_add_external_plugin(NppExec
	DISPLAY_NAME NppExec
	VERSION 0.8.12.1
	GIT https://github.com/d0vgan/nppexec.git
	COMMIT 7a5d279468b7929146758b0d24a78e0d76d17020
	SOURCES NppExec/src/CAnyCheckBox.cpp NppExec/src/CAnyComboBox.cpp NppExec/src/CAnyListBox.cpp NppExec/src/CAnyRichEdit.cpp NppExec/src/CAnyWindow.cpp NppExec/src/CFileModificationChecker.cpp NppExec/src/ChildProcess.cpp NppExec/src/CPopupListBox.cpp NppExec/src/cpp/CFileBufT.cpp NppExec/src/CSimpleLogger.cpp NppExec/src/CStaticOptionsManager.cpp NppExec/src/c_base/HexStr.c NppExec/src/c_base/int2str.c NppExec/src/c_base/MatchMask.c NppExec/src/c_base/max_int.c NppExec/src/c_base/PackDataStr.c NppExec/src/c_base/str2int.c NppExec/src/c_base/str_func.c NppExec/src/DirFileLister.cpp NppExec/src/DlgAdvancedOptions.cpp NppExec/src/DlgConsole.cpp NppExec/src/DlgConsoleEncoding.cpp NppExec/src/DlgConsoleOutputFilter.cpp NppExec/src/DlgDoExec.cpp NppExec/src/DlgHelpAbout.cpp NppExec/src/DlgInputBox.cpp NppExec/src/encodings/SysUniConv.cpp NppExec/src/fparser/fparser.cc NppExec/src/PickColorBtn.cpp NppExec/src/NppExec.cpp NppExec/src/NppExecCommandExecutor.cpp NppExec/src/NppExecScriptEngine.cpp NppExec/src/NppExecHelpers.cpp NppExec/src/NppScript.cpp NppExec/src/NppScriptList.cpp NppExec/src/PluginCommunication/NppExecPluginInterface.cpp NppExec/src/WarningAnalyzer.cpp NppExec/src/WarningMaskMatcher.cpp
	RC_FILES NppExec/src/NppExec.rc
	INCLUDES NppExec
	DEFINES NPPEXEC_EXPORTS _CRT_SECURE_NO_DEPRECATE)

nppmac_add_external_plugin(NppJavaPlugin
	DISPLAY_NAME "Java Plugin"
	VERSION 0.4.1
	GIT https://github.com/dominikcebula/npp-java-plugin.git
	COMMIT 644dbbb52ba11bb3f37c680f389eaaae4e41ee75
	SOURCES src/Actions/ClearConsoleAction.cpp src/Actions/CompileAction.cpp src/Actions/CompileAndRunAction.cpp src/Actions/RunAction.cpp src/Actions/ShowAboutDialogAction.cpp src/Actions/ShowHideJavaConsoleAction.cpp src/Command/CommandPrompt.cpp src/Command/LoggingCommand.cpp src/Components/AboutDialog.cpp src/Components/CRichEdit.cpp src/Components/NppGateway.cpp src/Components/JavaConsole.cpp src/Components/Utils.cpp src/Configuration/Configuration.cpp src/Configuration/ConfigurationProvider.cpp src/Configuration/ConfigurationValidator.cpp src/NppJavaPlugin.cpp src/PluginInfrastructure/DockingFeature/StaticDialog.cpp src/PluginInfrastructure/NppPluginMain.cpp src/PluginInfrastructure/PluginDefinition.cpp
	RC_FILES src/NppJavaPlugin.rc src/PluginInfrastructure/NppPluginMain.rc
	INCLUDES vs.proj src
	DEFINES NppJavaPlugin_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(Compose
	DISPLAY_NAME Compose
	VERSION 1.1.1
	GIT https://github.com/Coises/Compose-for-NotepadPlusPlus.git
	COMMIT 92e427968c88c00f64518863b4f50a3880b29779
	SOURCES src/About.cpp src/ComposeKeyDialog.cpp src/Configuration.cpp src/Framework/PluginFramework.cpp src/Framework/ScintillaCallEx.cpp src/LoadSequenceDefinitions.cpp src/ProcessCommands.cpp src/ProcessCompose.cpp src/Plugin.cpp src/ProcessNotifications.cpp
	RC_FILES src/resource.rc
	INCLUDES .
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

nppmac_add_external_plugin(ElasticTabstops
	DISPLAY_NAME ElasticTabstops
	VERSION 1.5.0
	GIT https://github.com/mariusv-github/ElasticTabstops.git
	COMMIT 291a9fcee6fcdb1f0c79667138e5377e103f1476
	SOURCES src/AboutDialog.cpp src/Hyperlinks.cpp src/ElasticTabstops.cpp
	RC_FILES src/resource.rc
	INCLUDES src src/Dialogs src/Parsers src/Npp src/Utilities
	DEFINES __STDC_WANT_SECURE_LIB__=1 _CRT_NONSTDC_NO_DEPRECATE INCLUDE_DEPRECATED_FEATURES)

nppmac_add_external_plugin(HexEditor
	DISPLAY_NAME HEX-Editor
	VERSION 0.9.14.0
	GIT https://github.com/chcg/NPP_HexEditor.git
	COMMIT 026debb7d650f87a27f1199d945c4b3518064662
	SOURCES Common/Ccpp/Common.cpp Common/Ccpp/dpiManagerV2.cpp Common/Ccpp/StaticDialog.cpp Common/Ccpp/NppDarkModeDummy.cpp HexEditor/src/HelpDlg/HelpDialog.cpp HexEditor/src/HelpDlg/URLCtrl.cpp HexEditor/src/Hex.cpp HexEditor/src/HEXDialog.cpp HexEditor/src/misc/ColorCombo.cpp HexEditor/src/misc/ColorPopup.cpp HexEditor/src/misc/ModifyMenu.cpp HexEditor/src/misc/MultiTypeCombo.cpp HexEditor/src/OptionDlg/ColumnDialog.cpp HexEditor/src/OptionDlg/OptionDialog.cpp HexEditor/src/UserDlg/CompareDialog.cpp HexEditor/src/UserDlg/FindReplaceDialog.cpp HexEditor/src/UserDlg/GotoDialog.cpp HexEditor/src/UserDlg/PatternDialog.cpp HexEditor/src/Utf8_16.cpp
	RC_FILES HexEditor/src/HEXDialog.rc
	INCLUDES HexEditor/projects/2003 HexEditor/src HexEditor/src/HelpDlg HexEditor/src/MISC HexEditor/src/OptionDlg HexEditor/src/UserDlg Common/Ccpp NativeLang/src
	DEFINES _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES HEXEDITOR_EXPORTS)

# LuaScript: Lua, which its Windows build makes a DLL of its own (Lua.vcxproj), is built into the plugin,
# configured for macOS (LUA_USE_MACOSX) as Windows' build is by _WIN32.
nppmac_add_external_plugin(LuaScript
	DISPLAY_NAME LuaScript
	VERSION 0.12
	GIT https://github.com/dail8859/LuaScript.git
	COMMIT a4dd230779d099f16962bc4c00f465a7d58e36d9
	SOURCES src/Dialogs/AboutDialog.cpp src/Dialogs/ConsoleDialog.cpp src/Dialogs/Hyperlinks.cpp src/Dialogs/StaticDialog.cpp src/IFaceTableMixer.cpp src/LuaConsole.cpp src/luawinfile/winfile.c src/NppExtensionAPI.cpp src/LuaScript.cpp src/NppIFaceTable.cpp src/SciIFaceTable.cpp src/SciTE/GUIWin.cpp src/SciTE/IFaceTable.cpp src/SciTE/LuaExtension.cpp src/SciTE/StyleWriter.cpp
		src/lua/src/lapi.c src/lua/src/lauxlib.c src/lua/src/lbaselib.c src/lua/src/lbitlib.c src/lua/src/lcode.c src/lua/src/lcorolib.c src/lua/src/lctype.c src/lua/src/ldblib.c src/lua/src/ldebug.c src/lua/src/ldo.c src/lua/src/ldump.c src/lua/src/lfunc.c src/lua/src/lgc.c src/lua/src/linit.c src/lua/src/liolib.c src/lua/src/llex.c src/lua/src/lmathlib.c src/lua/src/lmem.c src/lua/src/loadlib.c src/lua/src/lobject.c src/lua/src/lopcodes.c src/lua/src/loslib.c src/lua/src/lparser.c src/lua/src/lstate.c src/lua/src/lstring.c src/lua/src/lstrlib.c src/lua/src/ltable.c src/lua/src/ltablib.c src/lua/src/ltm.c src/lua/src/lundump.c src/lua/src/lutf8lib.c src/lua/src/lvm.c src/lua/src/lzio.c
	RC_FILES src/Dialogs/resource.rc src/Dialogs/Version.rc
	INCLUDES src src/SciTE src/lua/src src/Dialogs src/Npp src/Utilities
	DEFINES WIN32_LEAN_AND_MEAN __STDC_WANT_SECURE_LIB__=1 _CRT_NONSTDC_NO_DEPRECATE LUA_USE_MACOSX)

# MultiReplace: Lua's two programs (lua.c, luac.c, each with a main) are left out of the plugin.
nppmac_add_external_plugin(MultiReplace
	DISPLAY_NAME MultiReplace
	VERSION 6.1.0.37
	GIT https://github.com/daddel80/notepadpp-multireplace.git
	COMMIT db5065e524ea2b2344cd082488c7f4c9b47ef99f
	SOURCES src/ColumnTabs.cpp src/OnePassHits.cpp src/ConfigManager.cpp src/CsvListFormat.cpp src/DialogFreeze.cpp src/DirectoryWalk.cpp src/DPIManager.cpp src/DropTarget.cpp src/Encoding.cpp src/engine/EngineFactory.cpp src/engine/ExprTkEngine.cpp src/engine/Iformulaengine.cpp src/engine/LuaEngine.cpp src/exprtk/DateParse.cpp src/exprtk/EcmdParser.cpp src/exprtk/ExprTkPatternParser.cpp src/exprtk/FormatSpec.cpp src/exprtk/MatchHistory.cpp src/exprtk/MatchHistoryAnalysis.cpp src/exprtk/NumberParse.cpp src/FileDialogUtil.cpp src/FileLoad.cpp src/FileLoadPool.cpp src/image_data.cpp src/IniFileCache.cpp src/LanguageManager.cpp src/language_mapping.cpp src/ListCodec.cpp src/lua/lapi.c src/lua/lauxlib.c src/lua/lcode.c src/lua/ldebug.c src/lua/ldo.c src/lua/lgc.c src/lua/liolib.c src/lua/lparser.c src/lua/lstate.c src/lua/lstring.c src/lua/lstrlib.c src/lua/ltable.c src/lua/lvm.c src/AboutDialog.cpp src/lua/lbaselib.c src/lua/lcorolib.c src/lua/lctype.c src/lua/ldblib.c src/lua/ldump.c src/lua/lfunc.c src/lua/linit.c src/lua/llex.c src/lua/lmathlib.c src/lua/lmem.c src/lua/loadlib.c src/lua/lobject.c src/lua/lopcodes.c src/lua/loslib.c src/lua/ltablib.c src/lua/ltm.c src/lua/lundump.c src/lua/lutf8lib.c src/lua/lzio.c src/MultiReplaceConfigDialog.cpp src/MultiReplacePanel.cpp src/MultiReplace.cpp src/MultiReplacePanel_BatchUI.cpp src/MultiReplacePanel_FlowTabs.cpp src/MultiReplacePanel_FormulaDebug.cpp src/NppStyleKit.cpp src/NumericToken.cpp src/PluginDefinition.cpp src/ResultDock.cpp src/StaticDialog/StaticDialog.cpp src/StringUtils.cpp src/TandemDock.cpp src/UndoRedoManager.cpp
	RC_FILES src/MultiReplace.rc src/StaticDialog/resource.rc
	INCLUDES vs.proj src/lua
	DEFINES NPPPLUGINTEMPLATE_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1)

# MZC8051: its sources are written in GBK.
nppmac_add_external_plugin(MZC8051
	DISPLAY_NAME MZC8051
	VERSION 0.0.1
	GIT https://github.com/Jiangshan00001/npp_MZC8051.git
	COMMIT fb6eb297ee4f2405e6b6b2787ef05daf11e311c2
	SOURCES src/dir_file.cpp src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/myexecute.cpp src/NppPluginDemo.cpp src/osdialog.c src/osdialog_win.c src/PluginDefinition.cpp src/replace.cpp src/text_encode.cpp
	RC_FILES src/DockingFeature/goLine.rc src/NppPluginDemo.rc
	INCLUDES vs.proj
	DEFINES NPPPLUGINDEMO_EXPORTS _CRT_NONSTDC_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS=1
	ENCODING GBK)

nppmac_add_external_plugin(NppTextFX
	DISPLAY_NAME NppTextFX2
	VERSION 2.0.3
	GIT https://github.com/rainman74/NPPTextFX2.git
	COMMIT 71ec1ead277ef311ed3216df3c1ce19ea4985518
	SOURCES Minilzo/minilzo.cpp Src/CapsTables.cpp Src/MicroXML/MicroXML.cpp Src/NPPTextFX.cpp Src/Poppad/Popfind.cpp Src/Scintilla/UniConversion.cpp Src/TransformsA.cpp Src/TransformsW.cpp
	RC_FILES Src/NPPTextFX.rc
	INCLUDES VC SRC
	FORCED_INCLUDES VC/defines.h
	CHARACTER_SET MultiByte
	DEFINES TEXTFX_EXPORTS NPP_UNICODE)

# Papyrus: its package (dist) has the lexer's styles, their dark theme and extras beside the DLL.
nppmac_add_external_plugin(Papyrus
	DISPLAY_NAME "Papyrus Script Lexer"
	VERSION 1.2.2.354
	GIT https://github.com/blu3mania/npp-papyrus.git
	COMMIT 888d44af0b63368179755ca79d4f2be319b0377f
	SOURCES src/DllMain.cpp src/external/lexilla/Accessor.cxx src/external/lexilla/PropSetSimple.cxx src/external/lexilla/WordList.cxx src/external/npp/ColourPicker.cpp src/external/npp/ColourPopup.cpp src/external/npp/Common.cpp src/external/npp/DarkMode.cpp src/external/npp/NppDarkMode.cpp src/external/npp/StaticDialog.cpp src/external/npp/URLCtrl.cpp src/external/tinyxml2/tinyxml2.cpp src/external/XMessageBox/XMessageBox.cpp src/Plugin/Common/Game.cpp src/Plugin/Common/Logger.cpp src/Plugin/Common/NotepadPlusPlus.cpp src/Plugin/Common/StringUtil.cpp src/Plugin/Common/Timer.cpp src/Plugin/Common/Version.cpp src/Plugin/CompilationErrorHandling/ErrorAnnotator.cpp src/Plugin/CompilationErrorHandling/ErrorsWindow.cpp src/Plugin/Compiler/Compiler.cpp src/Plugin/Compiler/CompilerSettings.cpp src/Plugin/Lexer/Lexer.cpp src/Plugin/Lexer/LexerDefinition.cpp src/Plugin/Lexer/SimpleLexerBase.cpp src/Plugin/KeywordMatcher/KeywordMatcher.cpp src/Plugin/Plugin.cpp src/Plugin/PluginDefinition.cpp src/Plugin/Settings/Settings.cpp src/Plugin/Settings/SettingsDialog.cpp src/Plugin/Settings/SettingsStorage.cpp src/Plugin/UI/AboutDialog.cpp src/Plugin/UI/DialogBase.cpp src/Plugin/UI/MultiTabbedDialog.cpp
	RC_FILES src/Plugin/Resources.rc
	INCLUDES src src/external/gsl/include src/external/lexilla src/external/npp src/external/scintilla
	CXX_STANDARD 23
	PACKAGE_DIR dist)

# Plugins without a Visual Studio project: written by hand from their build scripts (MinGW, CMake,
# premake, nmake), with the libraries those fetch pinned (FETCH).

nppmac_add_external_plugin(HLASMLexer
	DISPLAY_NAME "HLASM Lexer - Column-Aware Syntax Highlighting"
	VERSION 1.1.1
	GIT https://github.com/Zaneham/hlasm-npp.git
	COMMIT fe7541565cd0e0af319b08600a80629dd46fb45e
	SOURCES src/hlasm_ilexer.cpp
	RC_FILES src/HLASMLexer.rc
	INCLUDES src obj
	CONCATENATE obj/hlasm_config_xml.h src/config_xml_head.inc config/HLASMLexer.xml src/config_xml_tail.inc)

nppmac_add_external_plugin(OCamlLexer
	DISPLAY_NAME "OCaml Lexer"
	VERSION 1.0.0
	GIT https://github.com/Zaneham/ocaml-npp.git
	COMMIT aaedbd468c710931352c6ca78badee5a6141e8f8
	SOURCES src/ocaml_ilexer.cpp
	RC_FILES src/OCamlLexer.rc
	INCLUDES src obj
	CONCATENATE obj/ocaml_config_xml.h src/config_xml_head.inc config/OCamlLexer.xml src/config_xml_tail.inc)

nppmac_add_external_plugin(NppFoldTail
	DISPLAY_NAME NppFoldTail
	VERSION 0.8.2
	GIT https://github.com/seantw/NppFoldTail.git
	COMMIT 1295965a3787ce60043b4b03856e26acb7542fc0
	SOURCES src/plugin.cpp
	RC_FILES src/plugin.rc
	INCLUDES src)

nppmac_add_external_plugin(BatPathIntelliSense
	DISPLAY_NAME "BAT Path IntelliSense"
	VERSION 0.1.0
	GIT https://github.com/nicehero/NppBatPathIntelliSense.git
	COMMIT c8f49b3b461b5db622d376ca16a9afc86f6123e5
	SOURCES src/Options.cpp src/PathCompletion.cpp src/Plugin.cpp src/Utf8.cpp
	RC_FILES src/BatPathIntelliSense.rc
	INCLUDES sdk src)

nppmac_add_external_plugin(SqlColumnHighlighter
	DISPLAY_NAME "SQL Column Highlighter"
	VERSION 1.0.0.0
	GIT https://github.com/zhgao/npp-sql-column-highlighter.git
	COMMIT c7a9fd9462c1215aaf2a432d354e0ebec48bb89c
	SOURCES src/Highlighter.cpp src/PluginMain.cpp
	RC_FILES src/version.rc
	INCLUDES src
	DEFINES 
	_CRT_SECURE_NO_WARNINGS )

nppmac_add_external_plugin(NX_ASCII_Database_Helper
	DISPLAY_NAME "NX ASCII Database Helper"
	VERSION 3.0.0
	GIT https://github.com/ZhuXuefengGitHub/NX_ASCII_Database_Helper.git
	COMMIT fa4ddf7043ae2b1fd3b1861b9c73ae10b86795c1
	SOURCES src/DockingFeature/GoToLineDlg.cpp src/DockingFeature/StaticDialog.cpp src/ExplorerDialog.cpp src/NppPluginDemo.cpp src/PluginDefinition.cpp
	RC_FILES src/ExplorerDialog.rc src/DockingFeature/goLine.rc src/NppPluginDemo.rc
	INCLUDES src src/DockingFeature)

nppmac_add_external_plugin(NppPluginOpenHost
	DISPLAY_NAME NppPluginOpenHost
	VERSION 1.1.0.0
	GIT https://github.com/jejemorg/NppPluginOpenHost.git
	COMMIT 1f3ce08dedc043b80dc4f821739d993ba84bdb83
	SOURCES src/NppPluginDemo.cpp src/PluginDefinition.cpp
	RC_FILES src/NppPluginDemo.rc
	INCLUDES src)

nppmac_add_external_plugin(VSCodeKeymapNpp
	DISPLAY_NAME "VSCode Keymap NPP"
	VERSION 0.2.0
	GIT https://github.com/14ag/vscode-to-notepadpp.git
	COMMIT 29209e4680ecffdd47203d9c79db8b3cb9c6258d
	SOURCES src/VSCodeKeymapPlugin.cpp
	INCLUDES include
	DEFINES WIN32_LEAN_AND_MEAN
	NOMINMAX )

nppmac_add_external_plugin(SimplifyJson
	DISPLAY_NAME "Simplify Json"
	VERSION 1.0.1
	GIT https://github.com/analystvipul/SimplifyJson.git
	COMMIT 231df4c7b8f506c086cceb74522f716cc220f712
	SOURCES src/PluginDefinition.cpp src/NppJsonTools.cpp src/JsonUtils.cpp
	RC_FILES src/NppJsonTools.rc
	INCLUDES src)

nppmac_add_external_plugin(NppVim
	DISPLAY_NAME NppVim
	VERSION 1.14.4.0
	GIT https://github.com/h-jangra/NppVim.git
	COMMIT e67c85e0f50962259f4cad7868136df4a57f77c0
	SOURCES src/CommandMode.cpp src/Marks.cpp src/Motion.cpp src/NormalMode.cpp src/Keymap.cpp src/Registers.cpp src/EditorOps.cpp src/NppVim.cpp src/TextObject.cpp src/Utils.cpp src/VisualMode.cpp src/ConfigManager.cpp src/OptionRegistry.cpp src/RcParser.cpp
	RC_FILES plugin/NppVim.rc
	INCLUDES include plugin)

nppmac_add_external_plugin(FileTools
	DISPLAY_NAME "Datamodder File Tools"
	VERSION 1.3.1.6
	GIT https://github.com/dm-utils/filetools.git
	COMMIT 5a61a8fe48366d1631bee893442f96fc6b4a1d2c
	SOURCES src/dllmain.cpp src/settings_dialog.cpp src/yaml_tidy.cpp src/yaml_convert.cpp src/json_tools.cpp src/csv_tools.cpp
	RC_FILES src/settings.rc
	INCLUDES . src)

nppmac_add_external_plugin(FormatSQL
	DISPLAY_NAME "Datamodder SQL Formatter"
	VERSION 1.3.2.17
	GIT https://github.com/dm-utils/formatsql.git
	COMMIT def4e18a33978a8c2e7c1605120ec612c9d0833c
	SOURCES src/dllmain.cpp src/formatter.cpp src/settings_dialog.cpp src/dialects.cpp src/settings_io.cpp
	RC_FILES src/settings.rc
	INCLUDES . src)

nppmac_add_external_plugin(QueryFiles
	DISPLAY_NAME "Datamodder Query Files"
	VERSION 1.3.1.0
	GIT https://github.com/dm-utils/queryfiles.git
	COMMIT 016236a03c024e857fc784ae4c7b783a935d12e9
	SOURCES src/dllmain.cpp src/settings_dialog.cpp src/result_grid.cpp src/sql_text.cpp src/duck_runner.cpp
	RC_FILES src/settings.rc
	INCLUDES . src)

nppmac_add_external_plugin(SessionMgr
	DISPLAY_NAME "Session Manager"
	VERSION 1.4.4
	GIT https://github.com/chcg/npp-session-manager.git
	COMMIT 407cded82e99c04537795246a7d201a3832fdd95
	SOURCES src/SessionMgr.cpp src/Settings.cpp src/DlgDelete.cpp src/DlgNew.cpp src/DlgRename.cpp src/DlgSessions.cpp src/DlgSettings.cpp src/DllMain.cpp src/Menu.cpp src/Properties.cpp src/ContextMenu.cpp src/System.cpp src/Util.cpp src/xml/tinyxml2.cpp
	RC_FILES src/res/resource.rc
	INCLUDES src src/npp src/xml src/res)

nppmac_add_external_plugin(NppLdapFormatter
	DISPLAY_NAME "LDAP Formatter"
	VERSION 1.0.0
	GIT https://github.com/ignaciojulio/npp-ldap-formatter.git
	COMMIT 21c66c880bdee037c61c01e756aa17127f39b163
	SOURCES src/BuildInfo.cpp src/LdapParser.cpp src/CoreBudget.cpp src/ScintillaGateway.cpp src/PluginController.cpp src/PluginDiagnostics.cpp src/PluginWorker.cpp src/PluginDefinition.cpp src/PluginExports.cpp
	INCLUDES include
	DEFINES WIN32_LEAN_AND_MEAN
	NOMINMAX 
	NPP_HEADERS)

# The nppplugins (premake): its build defines the revision, contact and repository strings.
nppmac_add_external_plugin(nppplugin_solutionhub
	DISPLAY_NAME "Solution Hub"
	VERSION 3.0.1
	GIT https://github.com/incrediblejr/nppplugins.git
	COMMIT ffa2e923f367c54582d5d8d45db0373b851d0a0b
	SOURCES nppplugin_shared/file/file_system.cpp nppplugin_shared/ini/simple_ini.cpp nppplugin_shared/json/json_reader.cpp nppplugin_shared/json/json_value.cpp nppplugin_shared/json/json_writer.cpp nppplugin_shared/json_aux/json_aux.cpp nppplugin_shared/npp/plugin/npp_plugin_interface.cpp nppplugin_shared/pugixml/pugixml.cpp nppplugin_shared/resizer/Resizer.cpp nppplugin_shared/string/string_utils.cpp nppplugin_shared/thread/thread.cpp nppplugin_shared/win32/ui_aux.cpp nppplugin_shared/win32/win_aux.cpp nppplugin_solutionhub/src/file_repository.cpp nppplugin_solutionhub/src/file_repository_common.cpp nppplugin_solutionhub/src/folder_monitor.cpp nppplugin_solutionhub/src/nppplugin.cpp nppplugin_solutionhub/src/nppplugin_solutionhub.cpp
	INCLUDES nppplugin_shared nppplugin_solutionhub/src
	DEFINES WIN32_LEAN_AND_MEAN VC_EXTRALEAN _CRT_SECURE_NO_WARNINGS "REVISION_HASH=\"ffa2e923f367c54582d5d8d45db0373b851d0a0b\"" "CONTACT_EMAIL=\"fredrik@incrediblejunior.com\"" "REPO_PATH=\"https://github.com/incrediblejr/nppplugins\"" "PLUGIN_VERSION=\"3.0.1\""
	PACKAGE_DIR nppplugin_solutionhub/resources
	NPP_HEADERS)

nppmac_add_external_plugin(nppplugin_ofis2
	DISPLAY_NAME "Open File In Solution"
	VERSION 3.0.1
	GIT https://github.com/incrediblejr/nppplugins.git
	COMMIT ffa2e923f367c54582d5d8d45db0373b851d0a0b
	SOURCES nppplugin_shared/file/file_system.cpp nppplugin_shared/ini/simple_ini.cpp nppplugin_shared/json/json_reader.cpp nppplugin_shared/json/json_value.cpp nppplugin_shared/json/json_writer.cpp nppplugin_shared/json_aux/json_aux.cpp nppplugin_shared/npp/plugin/npp_plugin_interface.cpp nppplugin_shared/pugixml/pugixml.cpp nppplugin_shared/resizer/Resizer.cpp nppplugin_shared/string/string_utils.cpp nppplugin_shared/thread/thread.cpp nppplugin_shared/win32/ui_aux.cpp nppplugin_shared/win32/win_aux.cpp nppplugin_ofis2/src/nppplugin.cpp nppplugin_ofis2/src/nppplugin_ofis2.cpp
	RC_FILES nppplugin_ofis2/src/nppplugin_ofis2.rc.template
	INCLUDES nppplugin_shared nppplugin_ofis2/src
	DEFINES WIN32_LEAN_AND_MEAN VC_EXTRALEAN _CRT_SECURE_NO_WARNINGS "REVISION_HASH=\"ffa2e923f367c54582d5d8d45db0373b851d0a0b\"" "CONTACT_EMAIL=\"fredrik@incrediblejunior.com\"" "REPO_PATH=\"https://github.com/incrediblejr/nppplugins\"" "PLUGIN_VERSION=\"3.0.1\""
	PACKAGE_DIR nppplugin_ofis2/resources
	NPP_HEADERS)

nppmac_add_external_plugin(nppplugin_solutionhub_ui
	DISPLAY_NAME "Solution Hub UI"
	VERSION 3.0.1
	GIT https://github.com/incrediblejr/nppplugins.git
	COMMIT ffa2e923f367c54582d5d8d45db0373b851d0a0b
	SOURCES nppplugin_shared/file/file_system.cpp nppplugin_shared/ini/simple_ini.cpp nppplugin_shared/json/json_reader.cpp nppplugin_shared/json/json_value.cpp nppplugin_shared/json/json_writer.cpp nppplugin_shared/json_aux/json_aux.cpp nppplugin_shared/npp/plugin/npp_plugin_interface.cpp nppplugin_shared/pugixml/pugixml.cpp nppplugin_shared/resizer/Resizer.cpp nppplugin_shared/string/string_utils.cpp nppplugin_shared/thread/thread.cpp nppplugin_shared/win32/ui_aux.cpp nppplugin_shared/win32/win_aux.cpp nppplugin_solutionhub_ui/src/nppplugin.cpp nppplugin_solutionhub_ui/src/nppplugin_solutionhub_ui.cpp nppplugin_solutionhub_ui/src/treelist/TreeListWnd.cpp
	RC_FILES nppplugin_solutionhub_ui/src/nppplugin_solutionhub_ui.rc.template
	INCLUDES nppplugin_shared nppplugin_solutionhub_ui/src
	DEFINES WIN32_LEAN_AND_MEAN VC_EXTRALEAN _CRT_SECURE_NO_WARNINGS "REVISION_HASH=\"ffa2e923f367c54582d5d8d45db0373b851d0a0b\"" "CONTACT_EMAIL=\"fredrik@incrediblejunior.com\"" "REPO_PATH=\"https://github.com/incrediblejr/nppplugins\"" "PLUGIN_VERSION=\"3.0.1\""
	NPP_HEADERS)

nppmac_add_external_plugin(nppplugin_solutiontools
	DISPLAY_NAME "Solution Tools"
	VERSION 3.0.1
	GIT https://github.com/incrediblejr/nppplugins.git
	COMMIT ffa2e923f367c54582d5d8d45db0373b851d0a0b
	SOURCES nppplugin_shared/file/file_system.cpp nppplugin_shared/ini/simple_ini.cpp nppplugin_shared/json/json_reader.cpp nppplugin_shared/json/json_value.cpp nppplugin_shared/json/json_writer.cpp nppplugin_shared/json_aux/json_aux.cpp nppplugin_shared/npp/plugin/npp_plugin_interface.cpp nppplugin_shared/pugixml/pugixml.cpp nppplugin_shared/resizer/Resizer.cpp nppplugin_shared/string/string_utils.cpp nppplugin_shared/thread/thread.cpp nppplugin_shared/win32/ui_aux.cpp nppplugin_shared/win32/win_aux.cpp nppplugin_solutiontools/src/nppplugin.cpp nppplugin_solutiontools/src/nppplugin_solutiontools.cpp
	INCLUDES nppplugin_shared nppplugin_solutiontools/src
	DEFINES WIN32_LEAN_AND_MEAN VC_EXTRALEAN _CRT_SECURE_NO_WARNINGS "REVISION_HASH=\"ffa2e923f367c54582d5d8d45db0373b851d0a0b\"" "CONTACT_EMAIL=\"fredrik@incrediblejunior.com\"" "REPO_PATH=\"https://github.com/incrediblejr/nppplugins\"" "PLUGIN_VERSION=\"3.0.1\""
	PACKAGE_DIR nppplugin_solutiontools/resources
	NPP_HEADERS)

# FixParser fetches pugixml with vcpkg and plugintemplate's headers with CMake.
nppmac_add_external_plugin(FixParser
	DISPLAY_NAME FixParser
	VERSION 0.1.3
	GIT https://github.com/chtaylo3/fix-parser.git
	COMMIT 96d8b05a49db78015b92b4bbf783cc7f6a6d846a
	SOURCES src/core/DelimiterNormalizer.cpp src/core/Dictionary.cpp src/core/DictionaryCache.cpp src/core/DictionarySelector.cpp src/core/FixParser.cpp src/core/FixTokenizer.cpp src/core/MessageFramer.cpp src/core/PrettyPrinter.cpp src/plugin/PluginDefinition.cpp src/plugin/dllmain.cpp src/ui/FieldRenderer.cpp src/ui/FixPanel.cpp npp-sdk/src/DockingFeature/StaticDialog.cpp third_party/pugixml/src/pugixml.cpp
	RC_FILES src/ui/FixPanel.rc
	INCLUDES src/core src/plugin src/ui generated npp-sdk/src npp-sdk/src/DockingFeature third_party/pugixml/src
	CONCATENATE generated/version.h src/plugin/version.h.in
	FETCH npp-sdk https://github.com/npp-plugins/plugintemplate.git d180d263d2275a9fe0ca35a8715cb48152d40bd3 third_party/pugixml https://github.com/zeux/pugixml.git db78afc2b7d8f043b4bc6b185635d949ea2ed2a8)

# HTMLTag fetches plugintemplate and SimpleIni with CPM.
nppmac_add_external_plugin(HTMLTag
	DISPLAY_NAME "HTML Tag"
	VERSION 1.6.1.0
	GIT https://github.com/rdipardo/nppHTMLTag.git
	COMMIT 4bac411c58a4f37706681756b3d4623b44bf15a0
	SOURCES src/LibNppPlugin/include/SciTextObjects.cpp src/LibNppPlugin/include/TextConv.cpp src/LibNppPlugin/FuncArray.cpp src/LibNppPlugin/PluginBase.cpp src/LibNppPlugin/LocalizedPlugin.cpp src/Forms/AboutDlg.cpp src/TagFinder.cpp src/Entities.cpp src/Unicode.cpp src/HtmlTag.cpp src/prj/DllMain.cpp deps/plugintemplate/src/DockingFeature/StaticDialog.cpp
	RC_FILES src/prj/HTMLTag.rc
	INCLUDES src src/Forms src/LibNppPlugin src/LibNppPlugin/include deps/plugintemplate/src deps/plugintemplate/src/DockingFeature deps/simpleini
	DEFINES _WINDOWS _USRDLL NOATOM NOGDICAPMASKS NOMETAFILE NOMINMAX NOMSG NOOPENFILE NORASTEROPS NOSCROLL NOSOUND NOTEXTMETRIC NOWH NOCOMM NOKANJI NOCRYPT NOMCX WIN32_LEAN_AND_MEAN SCI_DISABLE_PROVISIONAL _CRT_SECURE_NO_WARNINGS
	FETCH deps/plugintemplate https://github.com/npp-plugins/plugintemplate.git fa7f8557bec136b98ef287697c96dfa33d65e133 deps/simpleini https://github.com/brofield/simpleini.git 09c21bda1dc1b578fa55f4a005d79b0afd481296)

# NppMenuScripts is built with MinGW, whose windows.h gives C++ no min and max macros (NOMINMAX); its
# script fetches cJSON from master, pinned here at the latest release.
nppmac_add_external_plugin(NppMenuScripts
	DISPLAY_NAME NppMenuScripts
	VERSION 1.0.1.0
	GIT https://github.com/seantw/NppMenuScripts.git
	COMMIT 422219e80ce6e84e538e829fa429998dc686ae6c
	SOURCES src/main.cpp third_party/cJSON/cJSON.c
	RC_FILES src/resource.rc
	INCLUDES . src third_party/cJSON
	DEFINES NPPMENUSCRIPTS_DEBUG_DEFAULT=0 NOMINMAX
	FILES i18n.json menu-helper.js menu_helper.py MenuHelper.pm package.json workspace-template
	CONCATENATE third_party/cJSON.h third_party/cJSON/cJSON.h
	FETCH third_party/cJSON https://github.com/DaveGamble/cJSON.git c859b25da02955fef659d658b8f324b5cde87be3)
