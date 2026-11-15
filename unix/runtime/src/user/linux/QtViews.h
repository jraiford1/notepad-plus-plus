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

// What Linux's window backend (NativeWindow.cpp) shares with its menus (MenuBridge.cpp): the native
// view on Qt widgets.

#pragma once

#include "../Backend.h"

#include <QtCore/QPointer>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace nppmac::ui
{
	class SurfaceWidget;

	class NativeView
	{
	public:
		ViewOwner* _owner = nullptr;
		ViewKind _kind = ViewKind::child;
		TopLevelStyle _style;
		// A top level's or popup's window: a QWidget holding its menu bar (when it has one) above its
		// client area. Null for a child.
		QPointer<QWidget> _window;
		QVBoxLayout* _layout = nullptr;
		QPointer<QMenuBar> _menuBar;
		// The client area: the view's own surface widget, or a native control's widget (adopted).
		QPointer<QWidget> _widget;
		SurfaceWidget* _surface = nullptr;
		bool _adopted = false;
		bool _ready = false; // made: frame changes are reported from now on
	};

	// The menu bar of a top level's window, made when first asked for; and its removal.
	QMenuBar* menuBarFor(NativeView* view);
	void removeMenuBar(NativeView* view);
}
