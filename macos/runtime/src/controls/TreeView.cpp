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

// The tree view control (SysTreeView32): items with text, images, state and data; expanding and
// collapsing, selection, label editing, sorting, dragging (TVN_BEGINDRAG), custom draw, keyboard and
// mouse, and a vertical scroll bar. An HTREEITEM is the address of the item.

#include "ScrollBar.h"

#include <cwctype>
#include <memory>
#include <unordered_set>

namespace nppmac::controls
{
	namespace
	{
		constexpr int defaultIndent = 19;
		constexpr int buttonSize = 9;
		constexpr int imageGap = 3;
		constexpr int textPadding = 2;
		constexpr int wheelRows = 3;
		constexpr int dragThreshold = 4;
		constexpr UINT_PTR editSubclassId = 0x7E1D;

		struct TreeItem
		{
			TreeItem* _parent = nullptr;
			std::vector<std::unique_ptr<TreeItem>> _children;
			std::u16string _text;
			bool _textCallback = false;
			int _image = 0;
			int _selectedImage = 0;
			UINT _state = 0;
			LPARAM _param = 0;
			int _childrenFlag = 0; // cChildren: 1, 0, or I_CHILDRENCALLBACK
			int _depth = 0;        // 0 for top-level items
		};

		struct TreeView
		{
			TreeItem _root;                  // invisible; its children are the top-level items
			std::unordered_set<const TreeItem*> _live;
			TreeItem* _selected = nullptr;
			TreeItem* _dropHighlight = nullptr;
			TreeItem* _hot = nullptr;
			int _top = 0;                    // the first visible row
			int _itemHeight = 0;
			bool _customItemHeight = false;
			int _indent = defaultIndent;
			HIMAGELIST _images = nullptr;
			HIMAGELIST _stateImages = nullptr;
			COLORREF _background = CLR_NONE;
			COLORREF _textColor = CLR_NONE;
			COLORREF _lineColor = CLR_DEFAULT;
			DWORD _extendedStyle = 0;
			bool _focused = false;
			HWND _edit = nullptr;
			TreeItem* _editing = nullptr;
			TreeItem* _pressed = nullptr;    // the mouse went down on it; a move may start a drag
			POINT _pressPoint{};
			VerticalScrollBar _scrollBar;
			std::vector<TreeItem*> _rows;    // the visible items in order
			bool _rowsValid = false;
		};

		LONG styleOf(HWND hwnd)
		{
			return GetWindowLongW(hwnd, GWL_STYLE);
		}

		HTREEITEM toHandle(TreeItem* item)
		{
			return reinterpret_cast<HTREEITEM>(item);
		}

		TreeItem* toItem(const TreeView& tree, HTREEITEM handle)
		{
			auto* item = reinterpret_cast<TreeItem*>(handle);
			return item && tree._live.count(item) ? item : nullptr;
		}

		HWND notifyTarget(HWND hwnd)
		{
			return GetParent(hwnd);
		}

		void fillItem(const TreeItem* item, TVITEMW& out)
		{
			out = {};
			if (!item)
				return;
			out.hItem = toHandle(const_cast<TreeItem*>(item));
			out.state = item->_state;
			out.lParam = item->_param;
			out.iImage = item->_image;
			out.iSelectedImage = item->_selectedImage;
			out.mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		}

		LRESULT notifyTree(HWND hwnd, UINT code, UINT action, const TreeItem* oldItem, const TreeItem* newItem, POINT point = {})
		{
			NMTREEVIEWW notification{};
			notification.action = action;
			fillItem(oldItem, notification.itemOld);
			fillItem(newItem, notification.itemNew);
			notification.ptDrag = point;
			return sendNotify(hwnd, notifyTarget(hwnd), code, &notification.hdr);
		}

		const std::u16string& itemText(HWND hwnd, TreeItem* item, std::u16string& storage)
		{
			if (!item->_textCallback)
				return item->_text;
			// LPSTR_TEXTCALLBACK: the parent provides the text when it is needed.
			char16_t buffer[1024]{};
			NMTVDISPINFOW info{};
			info.item.mask = TVIF_TEXT;
			info.item.hItem = toHandle(item);
			info.item.lParam = item->_param;
			info.item.pszText = buffer;
			info.item.cchTextMax = 1024;
			sendNotify(hwnd, notifyTarget(hwnd), TVN_GETDISPINFOW, &info.hdr);
			storage = info.item.pszText ? info.item.pszText : u"";
			return storage;
		}

		bool hasChildren(HWND hwnd, TreeItem* item)
		{
			if (item->_childrenFlag == I_CHILDRENCALLBACK)
			{
				NMTVDISPINFOW info{};
				info.item.mask = TVIF_CHILDREN;
				info.item.hItem = toHandle(item);
				info.item.lParam = item->_param;
				sendNotify(hwnd, notifyTarget(hwnd), TVN_GETDISPINFOW, &info.hdr);
				return info.item.cChildren != 0;
			}
			return item->_childrenFlag > 0 || !item->_children.empty();
		}

		bool isExpanded(const TreeItem* item)
		{
			return (item->_state & TVIS_EXPANDED) != 0;
		}

		void collectRows(TreeItem* parent, std::vector<TreeItem*>& rows)
		{
			for (const auto& child : parent->_children)
			{
				rows.push_back(child.get());
				if (isExpanded(child.get()))
					collectRows(child.get(), rows);
			}
		}

		std::vector<TreeItem*>& rowsOf(TreeView& tree)
		{
			if (!tree._rowsValid)
			{
				tree._rows.clear();
				collectRows(&tree._root, tree._rows);
				tree._rowsValid = true;
			}
			return tree._rows;
		}

		void invalidateRows(TreeView& tree)
		{
			tree._rowsValid = false;
		}

		int rowOf(TreeView& tree, const TreeItem* item)
		{
			const auto& rows = rowsOf(tree);
			const auto found = std::find(rows.begin(), rows.end(), item);
			return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
		}

		RECT listArea(HWND hwnd)
		{
			RECT area{};
			GetClientRect(hwnd, &area);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
				InflateRect(&area, -1, -1);
			return area;
		}

		int visibleRows(HWND hwnd, const TreeView& tree)
		{
			const RECT area = listArea(hwnd);
			return std::max(1, static_cast<int>(area.bottom - area.top) / std::max(1, tree._itemHeight));
		}

		void updateScrollBar(HWND hwnd, TreeView& tree)
		{
			RECT area = listArea(hwnd);
			const int total = static_cast<int>(rowsOf(tree).size());
			tree._scrollBar.update({area.right - VerticalScrollBar::width, area.top, area.right, area.bottom}, total, visibleRows(hwnd, tree), tree._top);
		}

		RECT itemsArea(HWND hwnd, TreeView& tree)
		{
			RECT area = listArea(hwnd);
			updateScrollBar(hwnd, tree);
			if (tree._scrollBar.isNeeded() && !(styleOf(hwnd) & TVS_NOSCROLL))
				area.right -= VerticalScrollBar::width;
			return area;
		}

		void setTop(HWND hwnd, TreeView& tree, int top)
		{
			const int maximum = std::max(0, static_cast<int>(rowsOf(tree).size()) - visibleRows(hwnd, tree));
			top = std::clamp(top, 0, maximum);
			if (top != tree._top)
			{
				tree._top = top;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
		}

		void ensureVisible(HWND hwnd, TreeView& tree, TreeItem* item)
		{
			// Its ancestors are expanded first, without notifications, as TVM_ENSUREVISIBLE does.
			for (TreeItem* parent = item->_parent; parent && parent != &tree._root; parent = parent->_parent)
				parent->_state |= TVIS_EXPANDED;
			invalidateRows(tree);
			const int row = rowOf(tree, item);
			if (row < 0)
				return;
			if (row < tree._top)
				setTop(hwnd, tree, row);
			else if (row >= tree._top + visibleRows(hwnd, tree))
				setTop(hwnd, tree, row - visibleRows(hwnd, tree) + 1);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		// Where an item's parts are, in client coordinates (its row must be visible or not).
		struct ItemGeometry
		{
			RECT _row{};
			RECT _button{};
			RECT _image{};
			RECT _text{};
		};

		int imageSize(const TreeView& tree)
		{
			int width = 0;
			int height = 0;
			if (tree._images)
				ImageList_GetIconSize(tree._images, &width, &height);
			return width;
		}

		ItemGeometry geometryOf(HWND hwnd, TreeView& tree, TreeItem* item, int row)
		{
			ItemGeometry geometry;
			const RECT area = itemsArea(hwnd, tree);
			const int top = area.top + (row - tree._top) * tree._itemHeight;
			geometry._row = {area.left, top, area.right, top + tree._itemHeight};
			const bool linesAtRoot = (styleOf(hwnd) & TVS_LINESATROOT) != 0;
			int x = area.left + item->_depth * tree._indent + (linesAtRoot ? tree._indent : 0);
			if (linesAtRoot || item->_depth > 0)
			{
				const int buttonX = x - tree._indent / 2 - buttonSize / 2;
				const int buttonY = top + (tree._itemHeight - buttonSize) / 2;
				geometry._button = {buttonX, buttonY, buttonX + buttonSize, buttonY + buttonSize};
			}
			const int image = imageSize(tree);
			if (image > 0)
			{
				const int imageY = top + (tree._itemHeight - image) / 2;
				geometry._image = {x, imageY, x + image, imageY + image};
				x += image + imageGap;
			}
			std::u16string storage;
			const std::u16string& text = itemText(hwnd, item, storage);
			geometry._text = {x, top, x + textWidth(hwnd, text) + 2 * textPadding, top + tree._itemHeight};
			return geometry;
		}

		TreeItem* itemAtRow(TreeView& tree, int row)
		{
			const auto& rows = rowsOf(tree);
			return row >= 0 && row < static_cast<int>(rows.size()) ? rows[static_cast<size_t>(row)] : nullptr;
		}

		// TVHT_* flags and the item at a client point.
		TreeItem* hitTest(HWND hwnd, TreeView& tree, POINT point, UINT& flags)
		{
			flags = 0;
			RECT area = itemsArea(hwnd, tree);
			if (point.y < area.top)
				flags |= TVHT_ABOVE;
			if (point.y >= area.bottom)
				flags |= TVHT_BELOW;
			if (point.x < area.left)
				flags |= TVHT_TOLEFT;
			if (point.x >= area.right)
				flags |= TVHT_TORIGHT;
			if (flags)
				return nullptr;
			const int row = tree._top + (point.y - area.top) / std::max(1, tree._itemHeight);
			TreeItem* item = itemAtRow(tree, row);
			if (!item)
			{
				flags = TVHT_NOWHERE;
				return nullptr;
			}
			const ItemGeometry geometry = geometryOf(hwnd, tree, item, row);
			if (PtInRect(&geometry._button, point))
				flags = TVHT_ONITEMBUTTON;
			else if (PtInRect(&geometry._image, point))
				flags = TVHT_ONITEMICON;
			else if (PtInRect(&geometry._text, point))
				flags = TVHT_ONITEMLABEL;
			else if (point.x < geometry._text.left)
				flags = TVHT_ONITEMINDENT;
			else
				flags = TVHT_ONITEMRIGHT;
			return item;
		}

		bool expand(HWND hwnd, TreeView& tree, TreeItem* item, UINT action, bool notify)
		{
			if (!item || item == &tree._root)
				return false;
			const UINT code = action & 0x3;
			const bool expanded = isExpanded(item);
			bool expandIt = code == TVE_EXPAND || (code == TVE_TOGGLE && !expanded);
			if (code == TVE_COLLAPSE || (code == TVE_TOGGLE && expanded))
				expandIt = false;
			if (expandIt == expanded && !(action & TVE_COLLAPSERESET))
				return false;
			if (expandIt && !hasChildren(hwnd, item))
				return false;
			const UINT notifyAction = expandIt ? TVE_EXPAND : TVE_COLLAPSE;
			if (notify && notifyTree(hwnd, TVN_ITEMEXPANDINGW, notifyAction, nullptr, item))
				return false;
			if (expandIt)
				item->_state |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
			else
				item->_state &= ~TVIS_EXPANDED;
			if (!expandIt && (action & TVE_COLLAPSERESET))
			{
				item->_children.clear();
				item->_state &= ~TVIS_EXPANDEDONCE;
			}
			// A collapsed item takes the selection of a hidden descendant.
			if (!expandIt && tree._selected)
			{
				for (TreeItem* parent = tree._selected->_parent; parent; parent = parent->_parent)
				{
					if (parent == item)
					{
						tree._selected->_state &= ~TVIS_SELECTED;
						tree._selected = item;
						item->_state |= TVIS_SELECTED;
						break;
					}
				}
			}
			invalidateRows(tree);
			setTop(hwnd, tree, tree._top);
			InvalidateRect(hwnd, nullptr, FALSE);
			if (notify)
				notifyTree(hwnd, TVN_ITEMEXPANDEDW, notifyAction, nullptr, item);
			return true;
		}

		bool select(HWND hwnd, TreeView& tree, TreeItem* item, UINT cause)
		{
			if (item == tree._selected)
			{
				if (item)
					ensureVisible(hwnd, tree, item);
				return true;
			}
			if (notifyTree(hwnd, TVN_SELCHANGINGW, cause, tree._selected, item))
				return false;
			TreeItem* previous = tree._selected;
			if (previous)
				previous->_state &= ~TVIS_SELECTED;
			tree._selected = item;
			if (item)
			{
				item->_state |= TVIS_SELECTED;
				ensureVisible(hwnd, tree, item);
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			notifyTree(hwnd, TVN_SELCHANGEDW, cause, previous, item);
			return true;
		}

		void forget(TreeView& tree, TreeItem* item)
		{
			for (const auto& child : item->_children)
				forget(tree, child.get());
			tree._live.erase(item);
			if (tree._selected == item)
				tree._selected = nullptr;
			if (tree._dropHighlight == item)
				tree._dropHighlight = nullptr;
			if (tree._hot == item)
				tree._hot = nullptr;
			if (tree._pressed == item)
				tree._pressed = nullptr;
			if (tree._editing == item)
				tree._editing = nullptr;
		}

		void notifyDeleted(HWND hwnd, TreeItem* item)
		{
			for (const auto& child : item->_children)
				notifyDeleted(hwnd, child.get());
			notifyTree(hwnd, TVN_DELETEITEMW, 0, item, nullptr);
		}

		void deleteItem(HWND hwnd, TreeView& tree, TreeItem* item)
		{
			if (item == &tree._root)
			{
				for (const auto& child : tree._root._children)
					notifyDeleted(hwnd, child.get());
				for (const auto& child : tree._root._children)
					forget(tree, child.get());
				tree._root._children.clear();
				tree._top = 0;
			}
			else
			{
				notifyDeleted(hwnd, item);
				forget(tree, item);
				auto& siblings = item->_parent->_children;
				siblings.erase(std::find_if(siblings.begin(), siblings.end(), [item](const auto& child) { return child.get() == item; }));
			}
			invalidateRows(tree);
			setTop(hwnd, tree, tree._top);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		void applyItem(TreeItem& item, const TVITEMW& source)
		{
			if (source.mask & TVIF_TEXT)
			{
				item._textCallback = source.pszText == LPSTR_TEXTCALLBACKW;
				item._text = !item._textCallback && source.pszText ? source.pszText : u"";
			}
			if (source.mask & TVIF_IMAGE)
				item._image = source.iImage;
			if (source.mask & TVIF_SELECTEDIMAGE)
				item._selectedImage = source.iSelectedImage;
			if (source.mask & TVIF_STATE)
				item._state = (item._state & ~source.stateMask) | (source.state & source.stateMask);
			if (source.mask & TVIF_PARAM)
				item._param = source.lParam;
			if (source.mask & TVIF_CHILDREN)
				item._childrenFlag = source.cChildren;
		}

		TreeItem* insertItem(HWND hwnd, TreeView& tree, const TVINSERTSTRUCTW& insert)
		{
			TreeItem* parent = insert.hParent == nullptr || insert.hParent == TVI_ROOT ? &tree._root : toItem(tree, insert.hParent);
			if (!parent)
				return nullptr;
			auto created = std::make_unique<TreeItem>();
			created->_parent = parent;
			created->_depth = parent == &tree._root ? 0 : parent->_depth + 1;
			applyItem(*created, insert.item);
			created->_state &= ~(TVIS_SELECTED);
			TreeItem* item = created.get();

			auto& siblings = parent->_children;
			auto position = siblings.end();
			if (insert.hInsertAfter == TVI_FIRST)
			{
				position = siblings.begin();
			}
			else if (insert.hInsertAfter == TVI_SORT)
			{
				std::u16string storage;
				position = std::find_if(siblings.begin(), siblings.end(), [&](const auto& sibling)
				{
					std::u16string siblingStorage;
					return lstrcmpiW(itemText(hwnd, sibling.get(), siblingStorage).c_str(), itemText(hwnd, item, storage).c_str()) > 0;
				});
			}
			else if (insert.hInsertAfter != TVI_LAST && insert.hInsertAfter != nullptr)
			{
				TreeItem* after = toItem(tree, insert.hInsertAfter);
				auto found = std::find_if(siblings.begin(), siblings.end(), [after](const auto& sibling) { return sibling.get() == after; });
				if (found != siblings.end())
					position = found + 1;
			}
			siblings.insert(position, std::move(created));
			tree._live.insert(item);
			invalidateRows(tree);
			InvalidateRect(hwnd, nullptr, FALSE);
			return item;
		}

		TreeItem* sibling(TreeItem* item, int offset)
		{
			if (!item || !item->_parent)
				return nullptr;
			auto& siblings = item->_parent->_children;
			const auto found = std::find_if(siblings.begin(), siblings.end(), [item](const auto& child) { return child.get() == item; });
			const auto index = (found - siblings.begin()) + offset;
			return index >= 0 && index < static_cast<std::ptrdiff_t>(siblings.size()) ? siblings[static_cast<size_t>(index)].get() : nullptr;
		}

		TreeItem* nextItem(HWND hwnd, TreeView& tree, UINT relation, TreeItem* item)
		{
			switch (relation)
			{
				case TVGN_ROOT:
					return tree._root._children.empty() ? nullptr : tree._root._children.front().get();
				case TVGN_NEXT:
					return sibling(item, 1);
				case TVGN_PREVIOUS:
					return sibling(item, -1);
				case TVGN_PARENT:
					return item && item->_parent != &tree._root ? item->_parent : nullptr;
				case TVGN_CHILD:
				{
					TreeItem* parent = item ? item : &tree._root;
					return parent->_children.empty() ? nullptr : parent->_children.front().get();
				}
				case TVGN_FIRSTVISIBLE:
					return itemAtRow(tree, tree._top);
				case TVGN_NEXTVISIBLE:
				{
					const int row = rowOf(tree, item);
					return row < 0 ? nullptr : itemAtRow(tree, row + 1);
				}
				case TVGN_PREVIOUSVISIBLE:
				{
					const int row = rowOf(tree, item);
					return row <= 0 ? nullptr : itemAtRow(tree, row - 1);
				}
				case TVGN_LASTVISIBLE:
				{
					const auto& rows = rowsOf(tree);
					return rows.empty() ? nullptr : rows.back();
				}
				case TVGN_DROPHILITE:
					return tree._dropHighlight;
				case TVGN_CARET:
					return tree._selected;
				default:
					(void)hwnd;
					return nullptr;
			}
		}

		void paintButton(HDC hdc, const RECT& button, bool expanded, COLORREF color)
		{
			// Windows 10 draws chevrons: > collapsed, v expanded.
			HPEN pen = CreatePen(PS_SOLID, 1, color);
			HGDIOBJ oldPen = SelectObject(hdc, pen);
			const int x = (button.left + button.right) / 2;
			const int y = (button.top + button.bottom) / 2;
			if (expanded)
			{
				const POINT chevron[] = {{x - 4, y - 2}, {x, y + 2}, {x + 4, y - 2}};
				Polyline(hdc, chevron, 3);
			}
			else
			{
				const POINT chevron[] = {{x - 2, y - 4}, {x + 2, y}, {x - 2, y + 4}};
				Polyline(hdc, chevron, 3);
			}
			SelectObject(hdc, oldPen);
			DeleteObject(pen);
		}

		void paint(HWND hwnd, TreeView& tree, HDC hdc)
		{
			RECT client{};
			GetClientRect(hwnd, &client);
			const int saved = SaveDC(hdc);
			SelectObject(hdc, fontOf(hwnd));
			SetBkMode(hdc, TRANSPARENT);
			const COLORREF background = tree._background != CLR_NONE ? tree._background : GetSysColor(COLOR_WINDOW);
			const COLORREF textColor = tree._textColor != CLR_NONE ? tree._textColor : GetSysColor(COLOR_WINDOWTEXT);
			HBRUSH backgroundBrush = CreateSolidBrush(background);
			FillRect(hdc, &client, backgroundBrush);
			DeleteObject(backgroundBrush);
			if ((styleOf(hwnd) & WS_BORDER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
			{
				HBRUSH frame = CreateSolidBrush(RGB(130, 135, 144));
				FrameRect(hdc, &client, frame);
				DeleteObject(frame);
			}

			NMTVCUSTOMDRAW custom{};
			custom.nmcd.dwDrawStage = CDDS_PREPAINT;
			custom.nmcd.hdc = hdc;
			custom.nmcd.rc = client;
			const LRESULT prePaint = sendNotify(hwnd, notifyTarget(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);

			const RECT area = itemsArea(hwnd, tree);
			const int clip = SaveDC(hdc);
			IntersectClipRect(hdc, area.left, area.top, area.right, area.bottom);
			const LONG style = styleOf(hwnd);
			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			const auto& rows = rowsOf(tree);
			const int last = std::min(static_cast<int>(rows.size()), tree._top + visibleRows(hwnd, tree) + 1);
			for (int row = tree._top; row < last; ++row)
			{
				TreeItem* item = rows[static_cast<size_t>(row)];
				const ItemGeometry geometry = geometryOf(hwnd, tree, item, row);
				const bool selected = item == tree._selected && (tree._focused || (style & TVS_SHOWSELALWAYS));
				const bool highlighted = selected || item == tree._dropHighlight;

				COLORREF foreground = enabled ? textColor : GetSysColor(COLOR_GRAYTEXT);
				COLORREF itemBackground = CLR_NONE;
				if (highlighted)
				{
					itemBackground = tree._focused || item == tree._dropHighlight ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
					foreground = tree._focused || item == tree._dropHighlight ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor;
				}
				if (prePaint & CDRF_NOTIFYITEMDRAW)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
					custom.nmcd.rc = geometry._row;
					custom.nmcd.dwItemSpec = reinterpret_cast<DWORD_PTR>(item);
					custom.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (tree._focused && item == tree._selected ? CDIS_FOCUS : 0) | (item == tree._hot ? CDIS_HOT : 0);
					custom.nmcd.lItemlParam = item->_param;
					custom.clrText = foreground;
					custom.clrTextBk = itemBackground == CLR_NONE ? background : itemBackground;
					custom.iLevel = item->_depth;
					const LRESULT itemResult = sendNotify(hwnd, notifyTarget(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
					if (itemResult & CDRF_SKIPDEFAULT)
						continue;
					foreground = custom.clrText;
					if (highlighted || custom.clrTextBk != background)
						itemBackground = custom.clrTextBk;
				}

				if ((style & TVS_HASBUTTONS) && !IsRectEmpty(&geometry._button) && hasChildren(hwnd, item))
					paintButton(hdc, geometry._button, isExpanded(item), RGB(128, 128, 128));
				if (tree._images)
				{
					const int image = item == tree._selected ? item->_selectedImage : item->_image;
					if (image >= 0)
						ImageList_Draw(tree._images, image, hdc, geometry._image.left, geometry._image.top, ILD_TRANSPARENT);
				}
				RECT textRect = geometry._text;
				if (style & TVS_FULLROWSELECT)
					textRect.right = area.right;
				if (itemBackground != CLR_NONE)
				{
					HBRUSH highlight = CreateSolidBrush(itemBackground);
					FillRect(hdc, &textRect, highlight);
					DeleteObject(highlight);
				}
				SetTextColor(hdc, foreground);
				std::u16string storage;
				const std::u16string& text = itemText(hwnd, item, storage);
				RECT drawText = textRect;
				drawText.left += textPadding;
				HFONT underlined = nullptr;
				HGDIOBJ oldFont = nullptr;
				if ((style & TVS_TRACKSELECT) && item == tree._hot)
				{
					LOGFONTW logFont{};
					GetObjectW(fontOf(hwnd), sizeof(logFont), &logFont);
					logFont.lfUnderline = TRUE;
					underlined = CreateFontIndirectW(&logFont);
					oldFont = SelectObject(hdc, underlined);
				}
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &drawText, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
				if (underlined)
				{
					SelectObject(hdc, oldFont);
					DeleteObject(underlined);
				}
				if (tree._focused && item == tree._selected && !(style & TVS_FULLROWSELECT))
					DrawFocusRect(hdc, &textRect);
				if (prePaint & CDRF_NOTIFYITEMDRAW)
				{
					custom.nmcd.dwDrawStage = CDDS_ITEMPOSTPAINT;
					sendNotify(hwnd, notifyTarget(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
				}
			}
			RestoreDC(hdc, clip);
			if (tree._scrollBar.isNeeded() && !(style & TVS_NOSCROLL))
				tree._scrollBar.paint(hdc);
			if (prePaint & CDRF_NOTIFYPOSTPAINT)
			{
				custom.nmcd.dwDrawStage = CDDS_POSTPAINT;
				custom.nmcd.rc = client;
				sendNotify(hwnd, notifyTarget(hwnd), NM_CUSTOMDRAW, &custom.nmcd.hdr);
			}
			RestoreDC(hdc, saved);
		}

		void endLabelEdit(HWND hwnd, TreeView& tree, bool cancel)
		{
			if (!tree._edit)
				return;
			HWND edit = tree._edit;
			TreeItem* item = tree._editing;
			tree._edit = nullptr;
			tree._editing = nullptr;
			std::u16string text = windowText(edit);
			if (item && tree._live.count(item))
			{
				NMTVDISPINFOW info{};
				info.item.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM;
				info.item.hItem = toHandle(item);
				info.item.lParam = item->_param;
				info.item.pszText = cancel ? nullptr : text.data();
				info.item.cchTextMax = static_cast<int>(text.size() + 1);
				if (sendNotify(hwnd, notifyTarget(hwnd), TVN_ENDLABELEDITW, &info.hdr) && !cancel)
				{
					item->_text = text;
					item->_textCallback = false;
				}
			}
			DestroyWindow(edit);
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		LRESULT CALLBACK labelEditSubclass(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
		{
			HWND hwnd = GetParent(edit);
			TreeView* tree = dataOf<TreeView>(hwnd);
			switch (message)
			{
				case WM_GETDLGCODE:
					return DLGC_WANTALLKEYS;
				case WM_KEYDOWN:
					if (tree && (wParam == VK_RETURN || wParam == VK_ESCAPE))
					{
						endLabelEdit(hwnd, *tree, wParam == VK_ESCAPE);
						SetFocus(hwnd);
						return 0;
					}
					break;
				case WM_KILLFOCUS:
					if (tree && tree->_edit == edit)
						endLabelEdit(hwnd, *tree, false);
					break;
				default:
					break;
			}
			return DefSubclassProc(edit, message, wParam, lParam);
		}

		HWND beginLabelEdit(HWND hwnd, TreeView& tree, TreeItem* item)
		{
			if (!item)
				return nullptr;
			endLabelEdit(hwnd, tree, false);
			ensureVisible(hwnd, tree, item);
			NMTVDISPINFOW info{};
			info.item.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM;
			info.item.hItem = toHandle(item);
			info.item.lParam = item->_param;
			std::u16string storage;
			const std::u16string text = itemText(hwnd, item, storage);
			info.item.pszText = const_cast<char16_t*>(text.c_str());
			if (sendNotify(hwnd, notifyTarget(hwnd), TVN_BEGINLABELEDITW, &info.hdr))
				return nullptr;
			const ItemGeometry geometry = geometryOf(hwnd, tree, item, rowOf(tree, item));
			const RECT area = itemsArea(hwnd, tree);
			tree._editing = item;
			tree._edit = CreateWindowExW(0, WC_EDITW, text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
				geometry._text.left, geometry._text.top, std::max<int>(80, area.right - geometry._text.left), tree._itemHeight, hwnd, nullptr, nullptr, nullptr);
			SendMessageW(tree._edit, WM_SETFONT, reinterpret_cast<WPARAM>(fontOf(hwnd)), FALSE);
			SetWindowSubclass(tree._edit, labelEditSubclass, editSubclassId, 0);
			SetFocus(tree._edit);
			SendMessageW(tree._edit, EM_SETSEL, 0, -1);
			return tree._edit;
		}

		void sortChildren(HWND hwnd, TreeItem* parent, PFNTVCOMPARE compare, LPARAM data, bool recurse)
		{
			auto& children = parent->_children;
			std::stable_sort(children.begin(), children.end(), [&](const auto& a, const auto& b)
			{
				if (compare)
					return compare(a->_param, b->_param, data) < 0;
				std::u16string storageA;
				std::u16string storageB;
				return lstrcmpiW(itemText(hwnd, a.get(), storageA).c_str(), itemText(hwnd, b.get(), storageB).c_str()) < 0;
			});
			if (recurse)
			{
				for (const auto& child : children)
					sortChildren(hwnd, child.get(), compare, data, true);
			}
		}

		void keyDown(HWND hwnd, TreeView& tree, WPARAM key)
		{
			NMTVKEYDOWN keyDown{};
			keyDown.wVKey = static_cast<WORD>(key);
			sendNotify(hwnd, notifyTarget(hwnd), TVN_KEYDOWN, &keyDown.hdr);

			const auto& rows = rowsOf(tree);
			if (rows.empty())
				return;
			TreeItem* current = tree._selected;
			const int row = current ? rowOf(tree, current) : -1;
			const int page = std::max(1, visibleRows(hwnd, tree) - 1);
			TreeItem* target = nullptr;
			switch (key)
			{
				case VK_UP:
					target = itemAtRow(tree, std::max(0, row - 1));
					break;
				case VK_DOWN:
					target = itemAtRow(tree, std::min(static_cast<int>(rows.size()) - 1, row + 1));
					break;
				case VK_PRIOR:
					target = itemAtRow(tree, std::max(0, row - page));
					break;
				case VK_NEXT:
					target = itemAtRow(tree, std::min(static_cast<int>(rows.size()) - 1, row + page));
					break;
				case VK_HOME:
					target = rows.front();
					break;
				case VK_END:
					target = rows.back();
					break;
				case VK_LEFT:
					if (current && isExpanded(current))
						expand(hwnd, tree, current, TVE_COLLAPSE, true);
					else if (current && current->_parent != &tree._root)
						target = current->_parent;
					break;
				case VK_RIGHT:
					if (current && hasChildren(hwnd, current))
					{
						if (!isExpanded(current))
							expand(hwnd, tree, current, TVE_EXPAND, true);
						else if (!current->_children.empty())
							target = current->_children.front().get();
					}
					break;
				case VK_ADD:
					expand(hwnd, tree, current, TVE_EXPAND, true);
					break;
				case VK_SUBTRACT:
					expand(hwnd, tree, current, TVE_COLLAPSE, true);
					break;
				case VK_RETURN:
				{
					NMHDR header{};
					sendNotify(hwnd, notifyTarget(hwnd), NM_RETURN, &header);
					break;
				}
				case VK_F2:
					if (styleOf(hwnd) & TVS_EDITLABELS)
						beginLabelEdit(hwnd, tree, current);
					break;
				default:
					break;
			}
			if (target)
				select(hwnd, tree, target, TVC_BYKEYBOARD);
		}

		LRESULT getItem(HWND hwnd, TreeView& tree, TVITEMW& out)
		{
			TreeItem* item = toItem(tree, out.hItem);
			if (!item)
				return FALSE;
			if (out.mask & TVIF_TEXT)
			{
				std::u16string storage;
				const std::u16string& text = itemText(hwnd, item, storage);
				if (out.pszText && out.cchTextMax > 0)
					lstrcpynW(out.pszText, text.c_str(), out.cchTextMax);
			}
			if (out.mask & TVIF_IMAGE)
				out.iImage = item->_image;
			if (out.mask & TVIF_SELECTEDIMAGE)
				out.iSelectedImage = item->_selectedImage;
			if (out.mask & TVIF_STATE)
				out.state = item->_state & (out.stateMask ? out.stateMask : 0xFFFFFFFF);
			if (out.mask & TVIF_PARAM)
				out.lParam = item->_param;
			if (out.mask & TVIF_CHILDREN)
				out.cChildren = item->_childrenFlag == I_CHILDRENCALLBACK ? I_CHILDRENCALLBACK : (hasChildren(hwnd, item) ? 1 : 0);
			return TRUE;
		}

		LRESULT CALLBACK treeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			TreeView* tree = dataOf<TreeView>(hwnd);
			switch (message)
			{
				case WM_NCCREATE:
				{
					TreeView* created = createData<TreeView>(hwnd);
					created->_itemHeight = fontHeight(hwnd) + 4;
					return DefWindowProcW(hwnd, message, wParam, lParam);
				}
				case WM_DESTROY:
					if (tree)
					{
						endLabelEdit(hwnd, *tree, true);
						deleteItem(hwnd, *tree, &tree->_root);
					}
					return 0;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					HDC hdc = BeginPaint(hwnd, &ps);
					paint(hwnd, *tree, hdc);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_PRINTCLIENT:
					paint(hwnd, *tree, reinterpret_cast<HDC>(wParam));
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_SIZE:
					setTop(hwnd, *tree, tree->_top);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_SETFONT:
					DefWindowProcW(hwnd, message, wParam, lParam);
					if (!tree->_customItemHeight)
						tree->_itemHeight = std::max(fontHeight(hwnd) + 4, imageSize(*tree) + 2);
					if (LOWORD(lParam))
						InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_ENABLE:
				case WM_STYLECHANGED:
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case WM_GETDLGCODE:
					return DLGC_WANTARROWS | DLGC_WANTCHARS;
				case WM_SETFOCUS:
				case WM_KILLFOCUS:
				{
					tree->_focused = message == WM_SETFOCUS;
					InvalidateRect(hwnd, nullptr, FALSE);
					NMHDR header{};
					sendNotify(hwnd, notifyTarget(hwnd), tree->_focused ? NM_SETFOCUS : NM_KILLFOCUS, &header);
					return 0;
				}
				case WM_KEYDOWN:
					keyDown(hwnd, *tree, wParam);
					return 0;
				case WM_CHAR:
				{
					// Type-ahead: the next visible item starting with the character.
					if (wParam < 0x20)
						return 0;
					const auto& rows = rowsOf(*tree);
					const int start = tree->_selected ? rowOf(*tree, tree->_selected) : -1;
					for (size_t n = 1; n <= rows.size(); ++n)
					{
						TreeItem* candidate = rows[(static_cast<size_t>(start + 1) + n - 1) % rows.size()];
						std::u16string storage;
						const std::u16string& text = itemText(hwnd, candidate, storage);
						if (!text.empty() && std::towlower(text[0]) == std::towlower(static_cast<wint_t>(wParam)))
						{
							select(hwnd, *tree, candidate, TVC_BYKEYBOARD);
							break;
						}
					}
					return 0;
				}
				case WM_MOUSEWHEEL:
					setTop(hwnd, *tree, tree->_top - GET_WHEEL_DELTA_WPARAM(wParam) * wheelRows / WHEEL_DELTA);
					return 0;
				case WM_LBUTTONDOWN:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					endLabelEdit(hwnd, *tree, false);
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					if (tree->_scrollBar.contains(point) && !(styleOf(hwnd) & TVS_NOSCROLL))
					{
						setTop(hwnd, *tree, tree->_scrollBar.press(point));
						if (tree->_scrollBar.isDragging())
							SetCapture(hwnd);
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					UINT flags = 0;
					TreeItem* item = hitTest(hwnd, *tree, point, flags);
					if (!item)
						return 0;
					if (flags & TVHT_ONITEMBUTTON)
					{
						expand(hwnd, *tree, item, TVE_TOGGLE, true);
						return 0;
					}
					select(hwnd, *tree, item, TVC_BYMOUSE);
					tree->_pressed = item;
					tree->_pressPoint = point;
					SetCapture(hwnd);
					return 0;
				}
				case WM_MOUSEMOVE:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (tree->_scrollBar.isDragging())
					{
						setTop(hwnd, *tree, tree->_scrollBar.drag(point));
						return 0;
					}
					if (tree->_pressed && !(styleOf(hwnd) & TVS_DISABLEDRAGDROP)
						&& (std::abs(point.x - tree->_pressPoint.x) > dragThreshold || std::abs(point.y - tree->_pressPoint.y) > dragThreshold))
					{
						TreeItem* dragged = tree->_pressed;
						tree->_pressed = nullptr;
						ReleaseCapture();
						notifyTree(hwnd, TVN_BEGINDRAGW, 0, nullptr, dragged, tree->_pressPoint);
						return 0;
					}
					if (styleOf(hwnd) & TVS_TRACKSELECT)
					{
						UINT flags = 0;
						TreeItem* hot = hitTest(hwnd, *tree, point, flags);
						if (!(flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)))
							hot = nullptr;
						if (hot != tree->_hot)
						{
							tree->_hot = hot;
							TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
							TrackMouseEvent(&track);
							InvalidateRect(hwnd, nullptr, FALSE);
						}
					}
					return 0;
				}
				case WM_MOUSELEAVE:
					if (tree->_hot)
					{
						tree->_hot = nullptr;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				case WM_LBUTTONUP:
				{
					if (tree->_scrollBar.isDragging())
					{
						tree->_scrollBar.release();
						ReleaseCapture();
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					const bool clicked = tree->_pressed != nullptr;
					tree->_pressed = nullptr;
					if (GetCapture() == hwnd)
						ReleaseCapture();
					if (clicked)
					{
						NMHDR header{};
						sendNotify(hwnd, notifyTarget(hwnd), NM_CLICK, &header);
					}
					return 0;
				}
				case WM_CAPTURECHANGED:
					tree->_pressed = nullptr;
					tree->_scrollBar.release();
					return 0;
				case WM_LBUTTONDBLCLK:
				{
					const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					UINT flags = 0;
					TreeItem* item = hitTest(hwnd, *tree, point, flags);
					NMHDR header{};
					if (!sendNotify(hwnd, notifyTarget(hwnd), NM_DBLCLK, &header) && item && (flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)))
						expand(hwnd, *tree, item, TVE_TOGGLE, true);
					return 0;
				}
				case WM_RBUTTONDOWN:
				{
					if (GetFocus() != hwnd)
						SetFocus(hwnd);
					// While the right button is down, the item under it is drop-highlighted, as on Windows.
					UINT flags = 0;
					TreeItem* item = hitTest(hwnd, *tree, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, flags);
					tree->_dropHighlight = item;
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				case WM_RBUTTONUP:
				{
					tree->_dropHighlight = nullptr;
					InvalidateRect(hwnd, nullptr, FALSE);
					NMHDR header{};
					if (!sendNotify(hwnd, notifyTarget(hwnd), NM_RCLICK, &header))
					{
						POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
						ClientToScreen(hwnd, &screen);
						SendMessageW(GetParent(hwnd), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screen.x, screen.y));
					}
					return 0;
				}
				case WM_CONTEXTMENU:
					// Already sent to the parent after NM_RCLICK.
					return 0;
				case TVM_INSERTITEMW:
					return reinterpret_cast<LRESULT>(toHandle(insertItem(hwnd, *tree, *reinterpret_cast<const TVINSERTSTRUCTW*>(lParam))));
				case TVM_DELETEITEM:
				{
					HTREEITEM handle = reinterpret_cast<HTREEITEM>(lParam);
					if (!handle || handle == TVI_ROOT)
					{
						deleteItem(hwnd, *tree, &tree->_root);
						return TRUE;
					}
					TreeItem* item = toItem(*tree, handle);
					if (!item)
						return FALSE;
					deleteItem(hwnd, *tree, item);
					return TRUE;
				}
				case TVM_EXPAND:
					return expand(hwnd, *tree, toItem(*tree, reinterpret_cast<HTREEITEM>(lParam)), static_cast<UINT>(wParam), true);
				case TVM_GETITEMRECT:
				{
					auto* rect = reinterpret_cast<RECT*>(lParam);
					TreeItem* item = toItem(*tree, *reinterpret_cast<HTREEITEM*>(lParam));
					const int row = item ? rowOf(*tree, item) : -1;
					if (row < 0)
						return FALSE;
					const ItemGeometry geometry = geometryOf(hwnd, *tree, item, row);
					*rect = wParam ? geometry._text : geometry._row;
					return TRUE;
				}
				case TVM_GETCOUNT:
					return static_cast<LRESULT>(tree->_live.size());
				case TVM_GETINDENT:
					return tree->_indent;
				case TVM_SETINDENT:
					tree->_indent = std::max(static_cast<int>(wParam), buttonSize + 4);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				case TVM_GETIMAGELIST:
					return reinterpret_cast<LRESULT>(wParam == TVSIL_STATE ? tree->_stateImages : tree->_images);
				case TVM_SETIMAGELIST:
				{
					HIMAGELIST& list = wParam == TVSIL_STATE ? tree->_stateImages : tree->_images;
					HIMAGELIST previous = list;
					list = reinterpret_cast<HIMAGELIST>(lParam);
					if (!tree->_customItemHeight)
						tree->_itemHeight = std::max(fontHeight(hwnd) + 4, imageSize(*tree) + 2);
					InvalidateRect(hwnd, nullptr, FALSE);
					return reinterpret_cast<LRESULT>(previous);
				}
				case TVM_GETNEXTITEM:
				{
					TreeItem* item = toItem(*tree, reinterpret_cast<HTREEITEM>(lParam));
					return reinterpret_cast<LRESULT>(toHandle(nextItem(hwnd, *tree, static_cast<UINT>(wParam) & 0xF, item)));
				}
				case TVM_SELECTITEM:
				{
					TreeItem* item = toItem(*tree, reinterpret_cast<HTREEITEM>(lParam));
					switch (wParam & 0xF)
					{
						case TVGN_CARET:
							return select(hwnd, *tree, item, TVC_UNKNOWN);
						case TVGN_DROPHILITE:
							tree->_dropHighlight = item;
							InvalidateRect(hwnd, nullptr, FALSE);
							return TRUE;
						case TVGN_FIRSTVISIBLE:
							if (item)
							{
								ensureVisible(hwnd, *tree, item);
								setTop(hwnd, *tree, rowOf(*tree, item));
							}
							return TRUE;
						default:
							return FALSE;
					}
				}
				case TVM_GETITEMW:
					return getItem(hwnd, *tree, *reinterpret_cast<TVITEMW*>(lParam));
				case TVM_SETITEMW:
				{
					const auto* source = reinterpret_cast<const TVITEMW*>(lParam);
					TreeItem* item = toItem(*tree, source->hItem);
					if (!item)
						return FALSE;
					const bool wasExpanded = isExpanded(item);
					applyItem(*item, *source);
					if (isExpanded(item) != wasExpanded)
						invalidateRows(*tree);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case TVM_GETITEMSTATE:
				{
					TreeItem* item = toItem(*tree, reinterpret_cast<HTREEITEM>(wParam));
					return item ? (item->_state & static_cast<UINT>(lParam)) : 0;
				}
				case TVM_EDITLABELW:
					return reinterpret_cast<LRESULT>(beginLabelEdit(hwnd, *tree, toItem(*tree, reinterpret_cast<HTREEITEM>(lParam))));
				case TVM_GETEDITCONTROL:
					return reinterpret_cast<LRESULT>(tree->_edit);
				case TVM_ENDEDITLABELNOW:
					endLabelEdit(hwnd, *tree, wParam != 0);
					return TRUE;
				case TVM_GETVISIBLECOUNT:
					return visibleRows(hwnd, *tree);
				case TVM_HITTEST:
				{
					auto* info = reinterpret_cast<TVHITTESTINFO*>(lParam);
					info->hItem = toHandle(hitTest(hwnd, *tree, info->pt, info->flags));
					return reinterpret_cast<LRESULT>(info->hItem);
				}
				case TVM_CREATEDRAGIMAGE:
				{
					// An image list with the item's image; the drag image is not drawn on macOS.
					const int size = std::max(1, imageSize(*tree));
					return reinterpret_cast<LRESULT>(ImageList_Create(size, size, ILC_COLOR32, 1, 1));
				}
				case TVM_SORTCHILDREN:
				{
					HTREEITEM handle = reinterpret_cast<HTREEITEM>(lParam);
					TreeItem* parent = !handle || handle == TVI_ROOT ? &tree->_root : toItem(*tree, handle);
					if (!parent)
						return FALSE;
					sortChildren(hwnd, parent, nullptr, 0, wParam != 0);
					invalidateRows(*tree);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case TVM_SORTCHILDRENCB:
				{
					const auto* sort = reinterpret_cast<const TVSORTCB*>(lParam);
					TreeItem* parent = !sort->hParent || sort->hParent == TVI_ROOT ? &tree->_root : toItem(*tree, sort->hParent);
					if (!parent)
						return FALSE;
					sortChildren(hwnd, parent, sort->lpfnCompare, sort->lParam, false);
					invalidateRows(*tree);
					InvalidateRect(hwnd, nullptr, FALSE);
					return TRUE;
				}
				case TVM_ENSUREVISIBLE:
				{
					TreeItem* item = toItem(*tree, reinterpret_cast<HTREEITEM>(lParam));
					if (!item)
						return FALSE;
					ensureVisible(hwnd, *tree, item);
					return TRUE;
				}
				case TVM_SETITEMHEIGHT:
				{
					const int previous = tree->_itemHeight;
					if (static_cast<int>(wParam) == -1)
					{
						tree->_customItemHeight = false;
						tree->_itemHeight = std::max(fontHeight(hwnd) + 4, imageSize(*tree) + 2);
					}
					else
					{
						tree->_customItemHeight = true;
						tree->_itemHeight = std::max(1, static_cast<int>(wParam));
					}
					InvalidateRect(hwnd, nullptr, FALSE);
					return previous;
				}
				case TVM_GETITEMHEIGHT:
					return tree->_itemHeight;
				case TVM_SETBKCOLOR:
				{
					const COLORREF previous = tree->_background;
					tree->_background = static_cast<COLORREF>(lParam) == CLR_DEFAULT ? CLR_NONE : static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(previous == CLR_NONE ? static_cast<COLORREF>(-1) : previous);
				}
				case TVM_GETBKCOLOR:
					return static_cast<LRESULT>(tree->_background == CLR_NONE ? static_cast<COLORREF>(-1) : tree->_background);
				case TVM_SETTEXTCOLOR:
				{
					const COLORREF previous = tree->_textColor;
					tree->_textColor = static_cast<COLORREF>(lParam) == CLR_DEFAULT ? CLR_NONE : static_cast<COLORREF>(lParam);
					InvalidateRect(hwnd, nullptr, FALSE);
					return static_cast<LRESULT>(previous == CLR_NONE ? static_cast<COLORREF>(-1) : previous);
				}
				case TVM_GETTEXTCOLOR:
					return static_cast<LRESULT>(tree->_textColor == CLR_NONE ? static_cast<COLORREF>(-1) : tree->_textColor);
				case TVM_SETLINECOLOR:
				{
					const COLORREF previous = tree->_lineColor;
					tree->_lineColor = static_cast<COLORREF>(lParam);
					return static_cast<LRESULT>(previous);
				}
				case TVM_GETLINECOLOR:
					return static_cast<LRESULT>(tree->_lineColor);
				case TVM_SETEXTENDEDSTYLE:
				{
					const DWORD previous = tree->_extendedStyle;
					tree->_extendedStyle = (tree->_extendedStyle & ~static_cast<DWORD>(wParam)) | (static_cast<DWORD>(lParam) & static_cast<DWORD>(wParam));
					return previous;
				}
				case TVM_GETEXTENDEDSTYLE:
					return tree->_extendedStyle;
				case TVM_GETTOOLTIPS:
				case TVM_SETTOOLTIPS:
				case TVM_SETINSERTMARK:
				case TVM_GETISEARCHSTRINGW:
					return 0;
				default:
					return DefWindowProcW(hwnd, message, wParam, lParam);
			}
		}
	}

	void registerTreeView()
	{
		registerControl(WC_TREEVIEWW, treeProc);
	}
}
