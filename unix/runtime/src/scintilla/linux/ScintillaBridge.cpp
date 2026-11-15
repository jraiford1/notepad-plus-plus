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

// ScintillaBridge.h on Linux: Scintilla's own Qt widget (scintilla/qt/ScintillaEditBase). Its notify and
// command signals carry what the Cocoa layer's callback does (SCNotification, and WM_COMMAND's wParam),
// so the window class reports them the same way.

#include "../ScintillaBridge.h"

#include "ScintillaEditBase.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QPainter>

namespace nppmac::scintilla
{
	namespace
	{
		// Notepad++ shows its own editor context menu on WM_CONTEXTMENU: a right-click asks for it.
		class ContextMenuFilter : public QObject
		{
		public:
			ContextMenuFilter(QObject* parent, intptr_t window, ContextMenuFunc contextMenu)
				: QObject(parent), _window{window}, _contextMenu{contextMenu} {}

			bool eventFilter(QObject*, QEvent* event) override
			{
				if (event->type() != QEvent::ContextMenu || !_contextMenu)
					return false;
				const QPoint position = static_cast<QContextMenuEvent*>(event)->globalPos();
				_contextMenu(_window, position.x(), position.y());
				return true;
			}

		private:
			intptr_t _window;
			ContextMenuFunc _contextMenu;
		};
	}

	void* createView(intptr_t window, NotifyFunc notify, ContextMenuFunc contextMenu)
	{
		auto* view = new ScintillaEditBase(nullptr);
		view->resize(100, 100);
		QObject::connect(view, &ScintillaEditBase::notify, view, [window, notify](Scintilla::NotificationData* notification)
		{
			notify(window, cocoaNotify, 0, reinterpret_cast<uintptr_t>(notification));
		});
		QObject::connect(view, &ScintillaEditBase::command, view, [window, notify](Scintilla::uptr_t wParam, Scintilla::sptr_t lParam)
		{
			notify(window, cocoaCommand, wParam, static_cast<uintptr_t>(lParam));
		});
		auto* filter = new ContextMenuFilter(view, window, contextMenu);
		view->installEventFilter(filter);
		view->viewport()->installEventFilter(filter);
		return view;
	}

	void forgetView(void* view)
	{
		// The window manager deletes the widget with its view; nothing more reports to the window.
		if (auto* widget = static_cast<ScintillaEditBase*>(view))
		{
			QObject::disconnect(widget, &ScintillaEditBase::notify, nullptr, nullptr);
			QObject::disconnect(widget, &ScintillaEditBase::command, nullptr, nullptr);
		}
	}

	intptr_t message(void* view, unsigned message, uintptr_t wParam, intptr_t lParam)
	{
		return static_cast<ScintillaEditBase*>(view)->send(message, wParam, lParam);
	}

	void* printSurface(void* canvasHandle)
	{
		auto* painter = static_cast<QPainter*>(canvasHandle);
		return painter ? painter->device() : nullptr;
	}

	void insertText(void* view, const std::u16string& text)
	{
		// As typed: an input method's committed text, which Scintilla inserts as typing does.
		QInputMethodEvent event;
		event.setCommitString(QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size())));
		QCoreApplication::sendEvent(static_cast<ScintillaEditBase*>(view), &event);
	}
}
