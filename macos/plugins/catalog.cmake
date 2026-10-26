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
