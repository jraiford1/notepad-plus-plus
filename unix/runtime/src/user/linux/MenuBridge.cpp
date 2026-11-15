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

// MenuBridge.h on Linux: Qt menus. The menu bar is the owner window's own, in the window above its
// client area as on Windows; each menu asks for its items when it opens (WM_INITMENUPOPUP first), so it
// shows the program's state. Shortcuts are only shown: keys reach the window manager, whose accelerators
// run the commands.

#include "../MenuBridge.h"
#include "QtViews.h"

#include <QtGui/QAction>
#include <QtGui/QFont>
#include <QtWidgets/QMenu>

#include <functional>
#include <vector>

namespace nppmac::ui
{
	namespace
	{
		struct Bar
		{
			void* _context = nullptr;
			NativeView* _view = nullptr;
		};

		std::vector<Bar>& bars()
		{
			static auto* list = new std::vector<Bar>;
			return *list;
		}

		QString itemText(const MenuItemDescription& item)
		{
			QString text = QString::fromUtf16(item._title.data(), static_cast<qsizetype>(item._title.size()));
			text.replace('&', "&&"); // Qt's mnemonic marker; the titles come without Windows' own
			if (!item._shortcut.empty())
				text += '\t' + QString::fromUtf16(item._shortcut.data(), static_cast<qsizetype>(item._shortcut.size()));
			return text;
		}

		// Empties a menu or menu bar, its submenus deleted too (clear leaves them behind), and a menu bar's
		// right end.
		void clearMenu(QWidget* menu)
		{
			for (QMenu* submenu : menu->findChildren<QMenu*>(Qt::FindDirectChildrenOnly))
				submenu->deleteLater();
			if (auto* qmenu = qobject_cast<QMenu*>(menu))
				qmenu->clear();
			else if (auto* bar = qobject_cast<QMenuBar*>(menu))
			{
				bar->clear();
				if (QWidget* corner = bar->cornerWidget(Qt::TopRightCorner))
				{
					bar->setCornerWidget(nullptr, Qt::TopRightCorner);
					corner->hide();
					corner->deleteLater();
				}
			}
		}

		// Fills a Qt menu with a Win32 menu's items; a submenu asks for its own when it opens. A chosen
		// item calls chosen, or (chosen empty) leaves its id in the action's data, for exec's answer.
		void fill(QMenu* menu, uintptr_t handle, const MenuCallbacks& callbacks, void* context, const std::function<void(unsigned)>& chosen)
		{
			clearMenu(menu);
			if (!callbacks._items)
				return;
			for (const MenuItemDescription& item : callbacks._items(handle))
			{
				if (item._separator)
				{
					menu->addSeparator();
					continue;
				}
				if (item._submenu)
				{
					QMenu* submenu = menu->addMenu(itemText(item));
					submenu->setEnabled(item._enabled);
					const uintptr_t subHandle = item._submenu;
					QObject::connect(submenu, &QMenu::aboutToShow, submenu, [submenu, subHandle, callbacks, context, chosen]
					{
						if (callbacks._willOpen)
							callbacks._willOpen(subHandle, context);
						fill(submenu, subHandle, callbacks, context, chosen);
					});
					fill(submenu, subHandle, callbacks, context, chosen);
					continue;
				}
				QAction* action = menu->addAction(itemText(item));
				action->setEnabled(item._enabled);
				action->setData(item._id);
				if (item._checked || item._radio)
				{
					action->setCheckable(true);
					action->setChecked(item._checked);
				}
				if (item._default)
				{
					QFont font = action->font();
					font.setBold(true);
					action->setFont(font);
				}
				if (chosen)
				{
					const unsigned id = item._id;
					QObject::connect(action, &QAction::triggered, action, [chosen, id] { chosen(id); });
				}
			}
		}
	}

	void setMenuBar(uintptr_t menu, const MenuCallbacks& callbacks, void* context, const std::u16string&, NativeView* view)
	{
		QMenuBar* bar = menuBarFor(view);
		if (!bar)
			return;
		auto& list = bars();
		std::erase_if(list, [context](const Bar& entry) { return entry._context == context; });
		list.push_back({context, view});

		clearMenu(bar);
		if (!callbacks._items)
			return;
		const std::function<void(unsigned)> chosen = [callbacks, context](unsigned id)
		{
			if (callbacks._chosen)
				callbacks._chosen(id, context);
		};
		// From the first right-justified item on (Notepad++'s "+", "v" and "x"), the items sit at the bar's
		// right end, as on Windows: a menu bar of their own in its corner.
		QMenuBar* target = bar;
		for (const MenuItemDescription& item : callbacks._items(menu))
		{
			if (item._separator)
				continue;
			if (item._rightJustified && target == bar)
				target = new QMenuBar(bar);
			if (!item._submenu)
			{
				QAction* action = target->addAction(itemText(item));
				action->setEnabled(item._enabled);
				const unsigned id = item._id;
				QObject::connect(action, &QAction::triggered, action, [chosen, id] { chosen(id); });
				continue;
			}
			QMenu* submenu = target->addMenu(itemText(item));
			submenu->setEnabled(item._enabled);
			const uintptr_t subHandle = item._submenu;
			QObject::connect(submenu, &QMenu::aboutToShow, submenu, [submenu, subHandle, callbacks, context, chosen]
			{
				if (callbacks._willOpen)
					callbacks._willOpen(subHandle, context);
				fill(submenu, subHandle, callbacks, context, chosen);
			});
			fill(submenu, subHandle, callbacks, context, chosen);
		}
		// Placed once filled, so the bar lays it out at its full size.
		if (target != bar)
		{
			bar->setCornerWidget(target, Qt::TopRightCorner);
			target->show();
		}
	}

	void clearMenuBar(void* context)
	{
		auto& list = bars();
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if (it->_context == context)
			{
				removeMenuBar(it->_view);
				list.erase(it);
				return;
			}
		}
	}

	unsigned trackPopupMenu(uintptr_t menu, int screenX, int screenY, const MenuCallbacks& callbacks, void* context)
	{
		QMenu popup;
		if (callbacks._willOpen)
			callbacks._willOpen(menu, context);
		fill(&popup, menu, callbacks, context, {});
		QAction* chosen = popup.exec(QPoint(screenX, screenY));
		return chosen ? chosen->data().toUInt() : 0;
	}
}
