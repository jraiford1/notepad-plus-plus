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

// The window backend (Backend.h) on Linux: Qt 6 Widgets.
//
// A top level is a QWidget window holding its menu bar (when it has one) above its client area; a popup
// a frameless tool-tip window whose parent is its owner's window (Wayland places it relative to that);
// a child a widget in its parent's client area, placed by geometry and stacked with raise, lower and
// stackUnder. Qt's own record of where windows are is the screen coordinate system, so what the program
// places relative to a window stays right where Wayland does not tell where windows are.
//
// Custom-drawn views paint into a surface, a QImage with a QPainter kept on it, which GDI's canvas wraps;
// a paint event asks the owner to paint (WM_PAINT), then shows the surface.
//
// Keys reach the window manager from an application event filter, before any widget, as Windows virtual
// keys; a native control (Scintilla) gets them back through forwardKey. Input methods compose through
// Qt's input method events, which go to the window manager's focus view.

#include "QtViews.h"

#include "platform/Platform.h"

#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QMimeData>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QCloseEvent>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QStyleHints>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyle>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unistd.h>

namespace nppmac::ui
{
	namespace
	{
		const bool g_testRun = std::getenv("NPPMAC_TEST_RUN") != nullptr;
		// 96 dots per inch: GDI's fonts are sized for it (gdi/linux/Canvas.cpp).
		constexpr int dotsPerMeter = 3780;
		const QColor surfaceGrey(240, 240, 240); // AppKit's window background, as the Mac's surfaces start

		bool haveApplication()
		{
			return qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr;
		}

		std::u16string toU16(const QString& text)
		{
			return {reinterpret_cast<const char16_t*>(text.utf16()), static_cast<size_t>(text.size())};
		}

		QString toQString(const std::u16string& text)
		{
			return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
		}

		Rect toRect(const QRect& rect)
		{
			return {rect.left(), rect.top(), rect.left() + rect.width(), rect.top() + rect.height()};
		}

		QRect toQRect(const Rect& rect)
		{
			return {rect._left, rect._top, std::max(0, rect.width()), std::max(0, rect.height())};
		}

		unsigned modifiersOf(Qt::KeyboardModifiers modifiers)
		{
			unsigned result = 0;
			if (modifiers & Qt::ShiftModifier)
				result |= modifierShift;
			if (modifiers & Qt::ControlModifier)
				result |= modifierControl;
			if (modifiers & Qt::AltModifier)
				result |= modifierAlt;
			return result;
		}

		// --- Keys: Windows virtual keys ------------------------------------------------------------

		namespace vk
		{
			constexpr unsigned char back = 0x08, tab = 0x09, clear = 0x0C, returnKey = 0x0D, shift = 0x10, control = 0x11, menu = 0x12,
				pause = 0x13, capital = 0x14, escape = 0x1B, space = 0x20, prior = 0x21, next = 0x22, end = 0x23, home = 0x24, left = 0x25,
				up = 0x26, right = 0x27, down = 0x28, snapshot = 0x2C, insert = 0x2D, deleteKey = 0x2E, help = 0x2F, leftWindows = 0x5B,
				apps = 0x5D, numpad0 = 0x60, multiply = 0x6A, add = 0x6B, subtract = 0x6D, decimal = 0x6E, divide = 0x6F, f1 = 0x70,
				numLock = 0x90, scroll = 0x91, oem1 = 0xBA, oemPlus = 0xBB, oemComma = 0xBC, oemMinus = 0xBD, oemPeriod = 0xBE, oem2 = 0xBF,
				oem3 = 0xC0, oem4 = 0xDB, oem5 = 0xDC, oem6 = 0xDD, oem7 = 0xDE, oem102 = 0xE2;
		}

		// Windows' virtual key for a Qt key: letters and digits by what the layout makes of them (as
		// Windows' virtual keys are), the rest by name, and what is left by its place on the keyboard
		// (XKB key codes, a US layout's punctuation, as the Mac reads its key codes).
		unsigned char virtualKeyFor(const QKeyEvent& event)
		{
			const int key = event.key();
			const bool keypad = (event.modifiers() & Qt::KeypadModifier) != 0;
			switch (key)
			{
				case Qt::Key_Return:
				case Qt::Key_Enter:
					return vk::returnKey;
				case Qt::Key_Tab:
				case Qt::Key_Backtab:
					return vk::tab;
				case Qt::Key_Backspace:
					return vk::back;
				case Qt::Key_Escape:
					return vk::escape;
				case Qt::Key_Delete:
					return vk::deleteKey;
				case Qt::Key_Insert:
					return vk::insert;
				case Qt::Key_Home:
					return vk::home;
				case Qt::Key_End:
					return vk::end;
				case Qt::Key_PageUp:
					return vk::prior;
				case Qt::Key_PageDown:
					return vk::next;
				case Qt::Key_Left:
					return vk::left;
				case Qt::Key_Right:
					return vk::right;
				case Qt::Key_Up:
					return vk::up;
				case Qt::Key_Down:
					return vk::down;
				case Qt::Key_Shift:
					return vk::shift;
				case Qt::Key_Control:
					return vk::control;
				case Qt::Key_Alt:
				case Qt::Key_AltGr:
					return vk::menu;
				case Qt::Key_Meta:
				case Qt::Key_Super_L:
				case Qt::Key_Super_R:
					return vk::leftWindows;
				case Qt::Key_Menu:
					return vk::apps;
				case Qt::Key_CapsLock:
					return vk::capital;
				case Qt::Key_NumLock:
					return vk::numLock;
				case Qt::Key_ScrollLock:
					return vk::scroll;
				case Qt::Key_Pause:
					return vk::pause;
				case Qt::Key_Print:
					return vk::snapshot;
				case Qt::Key_Clear:
					return vk::clear;
				case Qt::Key_Help:
					return vk::help;
				default:
					break;
			}
			if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
				return static_cast<unsigned char>(vk::f1 + (key - Qt::Key_F1));
			if (keypad)
			{
				if (key >= Qt::Key_0 && key <= Qt::Key_9)
					return static_cast<unsigned char>(vk::numpad0 + (key - Qt::Key_0));
				switch (key)
				{
					case Qt::Key_Asterisk:
						return vk::multiply;
					case Qt::Key_Plus:
						return vk::add;
					case Qt::Key_Minus:
						return vk::subtract;
					case Qt::Key_Slash:
						return vk::divide;
					case Qt::Key_Period:
					case Qt::Key_Comma:
						return vk::decimal;
					default:
						break;
				}
			}
			if (key == Qt::Key_Space)
				return vk::space;
			if (key >= Qt::Key_A && key <= Qt::Key_Z)
				return static_cast<unsigned char>('A' + (key - Qt::Key_A));
			if (key >= Qt::Key_0 && key <= Qt::Key_9)
				return static_cast<unsigned char>('0' + (key - Qt::Key_0));

			// By place: XKB key codes (evdev's plus 8).
			switch (event.nativeScanCode())
			{
				case 20: return vk::oemMinus;
				case 21: return vk::oemPlus;
				case 34: return vk::oem4;
				case 35: return vk::oem6;
				case 47: return vk::oem1;
				case 48: return vk::oem7;
				case 49: return vk::oem3;
				case 51: return vk::oem5;
				case 59: return vk::oemComma;
				case 60: return vk::oemPeriod;
				case 61: return vk::oem2;
				case 94: return vk::oem102;
				default:
					break;
			}
			static const std::pair<quint32, char> places[] = {
				{10, '1'}, {11, '2'}, {12, '3'}, {13, '4'}, {14, '5'}, {15, '6'}, {16, '7'}, {17, '8'}, {18, '9'}, {19, '0'},
				{24, 'Q'}, {25, 'W'}, {26, 'E'}, {27, 'R'}, {28, 'T'}, {29, 'Y'}, {30, 'U'}, {31, 'I'}, {32, 'O'}, {33, 'P'},
				{38, 'A'}, {39, 'S'}, {40, 'D'}, {41, 'F'}, {42, 'G'}, {43, 'H'}, {44, 'J'}, {45, 'K'}, {46, 'L'},
				{52, 'Z'}, {53, 'X'}, {54, 'C'}, {55, 'V'}, {56, 'B'}, {57, 'N'}, {58, 'M'},
			};
			for (const auto& [code, character] : places)
			{
				if (code == event.nativeScanCode())
					return static_cast<unsigned char>(character);
			}
			return 0;
		}

		bool isModifierKey(int key)
		{
			return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_AltGr || key == Qt::Key_Meta
				|| key == Qt::Key_Super_L || key == Qt::Key_Super_R || key == Qt::Key_CapsLock;
		}

		// A key event kept for a native control (forwardKey), shared between whoever still holds it.
		struct HeldKey
		{
			std::atomic<int> _references{1};
			std::unique_ptr<QKeyEvent> _event;
		};

		thread_local bool t_forwardingKey = false;

		// The views of the program's own windows: what the key filter takes keys from.
		bool isRuntimeWindow(QObject* object)
		{
			auto* widget = qobject_cast<QWidget*>(object);
			return widget && widget->window() && widget->window()->property("nppmacWindow").toBool();
		}

		class KeyFilter : public QObject
		{
		public:
			bool eventFilter(QObject* object, QEvent* event) override
			{
				if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
					return false;
				// A key handed to a native control, an open menu's keys, and the keys of windows that are
				// not the program's (Qt's own dialogs) go where Qt sends them.
				if (t_forwardingKey || QApplication::activePopupWidget() || !isRuntimeWindow(object))
					return false;
				auto* key = static_cast<QKeyEvent*>(event);
				const bool down = event->type() == QEvent::KeyPress;
				if (isModifierKey(key->key()))
				{
					// The modifiers as they are after this key.
					Qt::KeyboardModifiers modifiers = key->modifiers();
					const Qt::KeyboardModifier own = key->key() == Qt::Key_Shift ? Qt::ShiftModifier
						: key->key() == Qt::Key_Control ? Qt::ControlModifier
						: (key->key() == Qt::Key_Alt ? Qt::AltModifier : Qt::NoModifier);
					if (own != Qt::NoModifier)
						modifiers = down ? (modifiers | own) : (modifiers & ~own);
					windowManagerModifiersChanged(modifiersOf(modifiers), virtualKeyFor(*key), down);
					return true;
				}
				KeyEvent native;
				native._down = down;
				native._repeat = key->isAutoRepeat();
				native._virtualKey = virtualKeyFor(*key);
				native._modifiers = modifiersOf(key->modifiers());
				native._characters = toU16(key->text());
				auto* held = new HeldKey;
				held->_event.reset(static_cast<QKeyEvent*>(key->clone()));
				native._nativeEvent = held;
				windowManagerKeyEvent(native);
				return true;
			}
		};

		// --- Test aids -----------------------------------------------------------------------------

		// NPPMAC_SNAPSHOT=<png>: after NPPMAC_SNAPSHOT_DELAY seconds (default 5) the view the window
		// manager chooses (or a Qt dialog open over it: a file, colour, font or print dialog, an alert) is
		// drawn into the file; with NPPMAC_DUMP_TREE the window tree goes to stderr first, and
		// NPPMAC_SNAPSHOT_EXIT ends the program afterwards.
		void scheduleSnapshot()
		{
			const char* path = std::getenv("NPPMAC_SNAPSHOT");
			if (!path || !*path)
				return;
			const char* delayText = std::getenv("NPPMAC_SNAPSHOT_DELAY");
			const double delay = delayText ? std::strtod(delayText, nullptr) : 5.0;
			const QString file = QString::fromLocal8Bit(path);
			QTimer::singleShot(static_cast<int>(delay * 1000), qApp, [file]
			{
				if (std::getenv("NPPMAC_DUMP_TREE"))
					windowManagerDumpTree();
				NativeView* view = windowManagerSnapshotView();
				QWidget* shown = view ? (view->_window ? view->_window.data() : view->_widget.data()) : nullptr;
				if (QWidget* dialog = QApplication::activeModalWidget())
					shown = dialog;
				if (shown)
					shown->grab().save(file, "PNG");
				if (std::getenv("NPPMAC_SNAPSHOT_EXIT"))
					::_exit(0);
			});
		}

		// NPPMAC_TEST_COMMAND=<id>[,<id>...] sends those menu commands, half a second apart, after
		// NPPMAC_TEST_COMMAND_DELAY seconds (default 2); click:<id> clicks the button with that control id
		// instead. As the Mac's.
		void scheduleCommands()
		{
			const char* command = std::getenv("NPPMAC_TEST_COMMAND");
			if (!command || !*command)
				return;
			const char* delayText = std::getenv("NPPMAC_TEST_COMMAND_DELAY");
			double delay = delayText ? std::atof(delayText) : 2.0;
			for (const char* next = command; *next;)
			{
				const bool click = std::strncmp(next, "click:", 6) == 0;
				if (click)
					next += 6;
				char* end = nullptr;
				const int id = static_cast<int>(std::strtol(next, &end, 10));
				if (end == next)
					break;
				QTimer::singleShot(static_cast<int>(delay * 1000), qApp, [click, id]
				{
					if (click)
						windowManagerTestClick(id);
					else
						windowManagerTestCommand(id);
				});
				delay += 0.5;
				next = *end == ',' ? end + 1 : end;
			}
		}

		// --- Event loop ----------------------------------------------------------------------------

		// Without an application (the runtime's tests), waiting is a condition wakeEventLoop signals.
		std::mutex g_wakeMutex;
		std::condition_variable g_wake;
		bool g_woken = false;

		Qt::CursorShape qtCursor(CursorShape shape)
		{
			switch (shape)
			{
				case CursorShape::arrow:
					return Qt::ArrowCursor;
				case CursorShape::iBeam:
					return Qt::IBeamCursor;
				case CursorShape::wait:
					return Qt::WaitCursor;
				case CursorShape::cross:
					return Qt::CrossCursor;
				case CursorShape::upArrow:
					return Qt::UpArrowCursor;
				case CursorShape::sizeNWSE:
					return Qt::SizeFDiagCursor;
				case CursorShape::sizeNESW:
					return Qt::SizeBDiagCursor;
				case CursorShape::sizeWE:
					return Qt::SizeHorCursor;
				case CursorShape::sizeNS:
					return Qt::SizeVerCursor;
				case CursorShape::sizeAll:
					return Qt::SizeAllCursor;
				case CursorShape::no:
					return Qt::ForbiddenCursor;
				case CursorShape::hand:
					return Qt::PointingHandCursor;
				case CursorShape::appStarting:
					return Qt::BusyCursor;
				case CursorShape::help:
					return Qt::WhatsThisCursor;
			}
			return Qt::ArrowCursor;
		}

		bool g_overrideCursor = false;
		Qt::CursorShape g_cursor = Qt::ArrowCursor;
		bool g_cursorHidden = false;

		void applyCursor()
		{
			if (!haveApplication())
				return;
			const QCursor cursor(g_cursorHidden ? Qt::BlankCursor : g_cursor);
			if (g_overrideCursor)
				QGuiApplication::changeOverrideCursor(cursor);
			else
			{
				QGuiApplication::setOverrideCursor(cursor);
				g_overrideCursor = true;
			}
		}

		NativeView* g_capture = nullptr;
		bool g_launched = false;
	}

	// --- Scroll bars -------------------------------------------------------------------------------

	namespace
	{
		constexpr int scrollRange = 10000;

		class RuntimeScrollBar : public QScrollBar
		{
		public:
			RuntimeScrollBar(Qt::Orientation orientation, QWidget* parent, NativeView* view) : QScrollBar(orientation, parent), _view{view}
			{
				setRange(0, scrollRange);
				connect(this, &QAbstractSlider::actionTriggered, this, [this](int action)
				{
					const bool vertical = this->orientation() == Qt::Vertical;
					switch (action)
					{
						case QAbstractSlider::SliderSingleStepAdd:
							report(vertical, ScrollPart::lineDown, 0);
							break;
						case QAbstractSlider::SliderSingleStepSub:
							report(vertical, ScrollPart::lineUp, 0);
							break;
						case QAbstractSlider::SliderPageStepAdd:
							report(vertical, ScrollPart::pageDown, 0);
							break;
						case QAbstractSlider::SliderPageStepSub:
							report(vertical, ScrollPart::pageUp, 0);
							break;
						case QAbstractSlider::SliderMove:
							_dragged = true;
							report(vertical, ScrollPart::thumbTrack, static_cast<double>(sliderPosition()) / scrollRange);
							break;
						default:
							break;
					}
				});
			}

			NativeView* _view;

		protected:
			// After any use of the bar: where the thumb was dropped, then the end (SB_ENDSCROLL), as the Mac
			// reports them.
			void mouseReleaseEvent(QMouseEvent* event) override
			{
				QScrollBar::mouseReleaseEvent(event);
				const bool vertical = orientation() == Qt::Vertical;
				if (_dragged)
					report(vertical, ScrollPart::thumbPosition, static_cast<double>(value()) / scrollRange);
				_dragged = false;
				report(vertical, ScrollPart::end, 0);
			}

		private:
			bool _dragged = false;

			void report(bool vertical, ScrollPart part, double value)
			{
				if (_view && _view->_owner)
					_view->_owner->nativeScroll(vertical, part, value);
			}
		};
	}

	// --- Surface widget ----------------------------------------------------------------------------

	class SurfaceWidget : public QWidget
	{
	public:
		SurfaceWidget(NativeView* view, QWidget* parent) : QWidget(parent), _view{view}
		{
			setMouseTracking(true);
			setAttribute(Qt::WA_InputMethodEnabled, true);
			setAttribute(Qt::WA_OpaquePaintEvent, true);
			setFocusPolicy(Qt::ClickFocus);
		}

		~SurfaceWidget() override
		{
			if (_painter.isActive())
				_painter.end();
		}

		NativeView* _view;
		bool _seeThrough = false;
		RuntimeScrollBar* _scrollBars[2]{}; // vertical, horizontal
		ScrollBarState _scrollStates[2];

		// The surface, its painter painting: made or resized to the widget's size at its device pixel
		// ratio; what it held stays at the top left, the rest starts grey (clear when see-through).
		QPainter* surface()
		{
			const qreal ratio = devicePixelRatioF();
			const QSize pixels(static_cast<int>(std::ceil(std::max(1, width()) * ratio)), static_cast<int>(std::ceil(std::max(1, height()) * ratio)));
			if (!_image.isNull() && _image.size() == pixels && _image.devicePixelRatio() == ratio)
			{
				if (!_painter.isActive())
					_painter.begin(&_image);
				return &_painter;
			}
			QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(ratio);
			image.setDotsPerMeterX(dotsPerMeter);
			image.setDotsPerMeterY(dotsPerMeter);
			image.fill(_seeThrough ? QColor(Qt::transparent) : surfaceGrey);
			if (_painter.isActive())
				_painter.end();
			if (!_image.isNull())
			{
				QPainter copy(&image);
				copy.setCompositionMode(QPainter::CompositionMode_Source);
				copy.drawImage(QPoint(0, 0), _image);
			}
			_image = image;
			_painter.begin(&_image);
			return &_painter;
		}

		void clearSurface(const QRect& rect)
		{
			QPainter* painter = surface();
			painter->save();
			painter->setClipping(false);
			painter->resetTransform();
			painter->setCompositionMode(QPainter::CompositionMode_Clear);
			painter->fillRect(rect, Qt::transparent);
			painter->restore();
		}

		// A view that is not shown has no pixels, as on Windows: all of it is painted again when shown.
		void givePixelsUp()
		{
			if (_image.isNull())
				return;
			if (_painter.isActive())
				_painter.end();
			_image = QImage();
			if (_view && _view->_owner)
				_view->_owner->nativePixelsLost();
		}

		void layoutScrollBars()
		{
			const int thickness = scrollBarThickness();
			RuntimeScrollBar* vertical = _scrollBars[0];
			RuntimeScrollBar* horizontal = _scrollBars[1];
			const bool verticalShown = vertical && _scrollStates[0]._visible;
			const bool horizontalShown = horizontal && _scrollStates[1]._visible;
			if (vertical)
			{
				vertical->setGeometry(width() - thickness, 0, thickness, height() - (horizontalShown ? thickness : 0));
				vertical->setVisible(verticalShown);
				vertical->raise();
			}
			if (horizontal)
			{
				horizontal->setGeometry(0, height() - thickness, width() - (verticalShown ? thickness : 0), thickness);
				horizontal->setVisible(horizontalShown);
				horizontal->raise();
			}
		}

	protected:
		void paintEvent(QPaintEvent* event) override
		{
			if (_view && _view->_owner && isVisible())
				_view->_owner->nativePaint({0, 0, width(), height()});
			QPainter painter(this);
			if (_image.isNull())
			{
				if (!_seeThrough)
					painter.fillRect(event->rect(), surfaceGrey);
				return;
			}
			painter.drawImage(QPoint(0, 0), _image);
		}

		void resizeEvent(QResizeEvent* event) override
		{
			QWidget::resizeEvent(event);
			layoutScrollBars();
			reportClientMoved();
		}

		void moveEvent(QMoveEvent* event) override
		{
			QWidget::moveEvent(event);
			reportClientMoved();
		}

		void hideEvent(QHideEvent* event) override
		{
			QWidget::hideEvent(event);
			givePixelsUp();
		}

		void showEvent(QShowEvent* event) override
		{
			QWidget::showEvent(event);
			update();
		}

		void mousePressEvent(QMouseEvent* event) override
		{
			mouse(event, MouseAction::down);
		}

		void mouseReleaseEvent(QMouseEvent* event) override
		{
			mouse(event, MouseAction::up);
		}

		void mouseDoubleClickEvent(QMouseEvent* event) override
		{
			mouse(event, MouseAction::doubleClick);
		}

		void mouseMoveEvent(QMouseEvent* event) override
		{
			mouse(event, MouseAction::move);
		}

		void wheelEvent(QWheelEvent* event) override
		{
			if (!_view || !_view->_owner)
				return;
			// Qt's wheel unit is Windows': 120 per notch.
			const QPoint angle = event->angleDelta();
			const Point position{static_cast<int>(std::floor(event->position().x())), static_cast<int>(std::floor(event->position().y()))};
			const unsigned modifiers = modifiersOf(event->modifiers());
			if (angle.y() != 0)
				_view->_owner->nativeMouse(MouseAction::wheel, MouseButton::none, position, modifiers, angle.y());
			if (angle.x() != 0)
				_view->_owner->nativeMouse(MouseAction::horizontalWheel, MouseButton::none, position, modifiers, -angle.x());
			event->accept();
		}

		void leaveEvent(QEvent* event) override
		{
			QWidget::leaveEvent(event);
			if (_view && _view->_owner)
				_view->_owner->nativeMouse(MouseAction::leave, MouseButton::none, {}, 0, 0);
		}

		void dragEnterEvent(QDragEnterEvent* event) override
		{
			if (_view && _view->_owner && _view->_owner->nativeAcceptsFiles() && !localFiles(event->mimeData()).empty())
				event->acceptProposedAction();
		}

		void dragMoveEvent(QDragMoveEvent* event) override
		{
			event->acceptProposedAction();
		}

		void dropEvent(QDropEvent* event) override
		{
			const std::vector<std::string> paths = localFiles(event->mimeData());
			if (paths.empty() || !_view || !_view->_owner)
				return;
			const QPointF position = event->position();
			_view->_owner->nativeFilesDropped(paths, {static_cast<int>(position.x()), static_cast<int>(position.y())});
			event->acceptProposedAction();
		}

		// Input methods compose for the window manager's focus view (this one, or another of the window).
		void inputMethodEvent(QInputMethodEvent* event) override
		{
			ViewOwner* owner = focusOwner();
			if (!owner)
				return;
			if (!event->commitString().isEmpty())
			{
				owner->nativeComposition({}, 0);
				owner->nativeInsertText(toU16(event->commitString()));
			}
			const QString preedit = event->preeditString();
			int caret = static_cast<int>(preedit.size());
			for (const QInputMethodEvent::Attribute& attribute : event->attributes())
			{
				if (attribute.type == QInputMethodEvent::Cursor)
					caret = std::clamp(attribute.start, 0, static_cast<int>(preedit.size()));
			}
			owner->nativeComposition(toU16(preedit), static_cast<size_t>(caret));
			event->accept();
		}

		QVariant inputMethodQuery(Qt::InputMethodQuery query) const override
		{
			if (query == Qt::ImEnabled)
				return true;
			if (query == Qt::ImCursorRectangle)
			{
				NativeView* focus = windowManagerFocusView();
				NativeView* target = focus && focus->_widget ? focus : _view;
				if (target && target->_owner && target->_widget)
				{
					const Rect caret = target->_owner->nativeCaretRect();
					const QPoint origin = target->_widget->mapTo(window(), QPoint(0, 0)) - mapTo(window(), QPoint(0, 0));
					return QRect(caret._left + origin.x(), caret._top + origin.y(), std::max(1, caret.width()), std::max(1, caret.height()));
				}
			}
			return QWidget::inputMethodQuery(query);
		}

	private:
		QImage _image;
		QPainter _painter;

		ViewOwner* focusOwner() const
		{
			NativeView* focus = windowManagerFocusView();
			if (focus && focus->_owner)
				return focus->_owner;
			return _view ? _view->_owner : nullptr;
		}

		void mouse(QMouseEvent* event, MouseAction action)
		{
			if (!_view || !_view->_owner)
				return;
			MouseButton button = MouseButton::none;
			if (action != MouseAction::move)
			{
				switch (event->button())
				{
					case Qt::LeftButton:
						button = MouseButton::left;
						break;
					case Qt::RightButton:
						button = MouseButton::right;
						break;
					case Qt::MiddleButton:
						button = MouseButton::middle;
						break;
					default:
						return;
				}
			}
			const QPointF position = event->position();
			_view->_owner->nativeMouse(action, button, {static_cast<int>(std::floor(position.x())), static_cast<int>(std::floor(position.y()))},
				modifiersOf(event->modifiers()), 0);
		}

		static std::vector<std::string> localFiles(const QMimeData* data)
		{
			std::vector<std::string> paths;
			if (data && data->hasUrls())
			{
				for (const QUrl& url : data->urls())
				{
					if (url.isLocalFile())
						paths.push_back(url.toLocalFile().toStdString());
				}
			}
			return paths;
		}

		// A top level's client area moving within its window (a menu bar came or went), or resizing.
		void reportClientMoved();
	};

	namespace
	{
		void reportFrame(NativeView* view)
		{
			if (view && view->_ready && view->_owner && view->_window)
				view->_owner->nativeFrameChanged(windowRectOnScreen(view), clientRectOnScreen(view));
		}

		class TopWindow : public QWidget
		{
		public:
			TopWindow(NativeView* view, QWidget* parent, Qt::WindowFlags flags) : QWidget(parent, flags), _view{view}
			{
				setProperty("nppmacWindow", true);
				setAttribute(Qt::WA_DeleteOnClose, false);
			}

			NativeView* _view;

		protected:
			void closeEvent(QCloseEvent* event) override
			{
				event->ignore();
				if (_view && _view->_owner)
					_view->_owner->nativeCloseRequested();
			}

			void moveEvent(QMoveEvent* event) override
			{
				QWidget::moveEvent(event);
				reportFrame(_view);
			}

			void resizeEvent(QResizeEvent* event) override
			{
				QWidget::resizeEvent(event);
				reportFrame(_view);
			}

			void changeEvent(QEvent* event) override
			{
				QWidget::changeEvent(event);
				if (!_view || !_view->_owner)
					return;
				if (event->type() == QEvent::ActivationChange)
					_view->_owner->nativeActivated(isActiveWindow());
				else if (event->type() == QEvent::WindowStateChange)
					_view->_owner->nativeMinimized(isMinimized());
			}
		};

		// The window an owned top level or popup is parented to: its owner's window, else (for a popup)
		// the active one.
		QWidget* ownerWindow(NativeView* owner, bool popup)
		{
			QWidget* window = owner ? (owner->_window ? owner->_window.data() : (owner->_widget ? owner->_widget->window() : nullptr)) : nullptr;
			if (!window && popup && haveApplication())
				window = QApplication::activeWindow();
			return window;
		}

		Qt::WindowFlags windowFlags(ViewKind kind, const TopLevelStyle& style)
		{
			if (kind == ViewKind::popup)
				return Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint;
			Qt::WindowFlags flags = Qt::Window | Qt::CustomizeWindowHint;
			if (!style._titled)
				flags |= Qt::FramelessWindowHint;
			else
				flags |= Qt::WindowTitleHint;
			if (style._closable)
				flags |= Qt::WindowCloseButtonHint;
			if (style._minimizable)
				flags |= Qt::WindowMinimizeButtonHint;
			if (style._resizable)
				flags |= Qt::WindowMaximizeButtonHint;
			if (style._topMost)
				flags |= Qt::WindowStaysOnTopHint;
			return flags;
		}

		// A window for a view: its client area (the given widget, or a new surface) under its menu bar.
		void makeWindow(NativeView* view, ViewKind kind, NativeView* owner, const Rect& frame, const TopLevelStyle& style, QWidget* client)
		{
			auto* window = new TopWindow(view, ownerWindow(owner, kind == ViewKind::popup), windowFlags(kind, style));
			auto* layout = new QVBoxLayout(window);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);
			if (!client)
			{
				view->_surface = new SurfaceWidget(view, window);
				client = view->_surface;
			}
			else
				client->setParent(window);
			layout->addWidget(client, 1);
			client->show(); // shown with its window
			window->setGeometry(toQRect(frame));
			if (!style._resizable && kind == ViewKind::topLevel)
				window->setFixedSize(window->size());
			view->_window = window;
			view->_layout = layout;
			view->_widget = client;
		}

		// Deletes a widget once the event loop is back, keeping the program's other windows (owned
		// windows parented to it) out of it.
		void retire(QWidget* widget)
		{
			if (!widget)
				return;
			for (QObject* child : widget->children())
			{
				auto* childWidget = qobject_cast<QWidget*>(child);
				if (childWidget && childWidget->isWindow())
					childWidget->setParent(nullptr, childWidget->windowFlags());
			}
			if (auto* surface = dynamic_cast<SurfaceWidget*>(widget))
				surface->_view = nullptr;
			if (auto* top = dynamic_cast<TopWindow*>(widget))
				top->_view = nullptr;
			widget->hide();
			widget->deleteLater();
		}
	}

	void SurfaceWidget::reportClientMoved()
	{
		if (_view && _view->_window && _view->_widget == this)
			reportFrame(_view);
	}

	// --- Application ---------------------------------------------------------------------------------

	// The program is ending: the windows waiting to be deleted go, then the application, while Qt and its
	// windowing are all still there. Left to the process's own clean-up, Qt deleted them after parts of
	// itself had gone, and crashed.
	void shutDownApplication()
	{
		if (!QCoreApplication::instance())
			return;
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		delete QCoreApplication::instance();
	}

	void createApplication()
	{
		// A test run's windows never reach the screen: Qt draws them off it.
		if (g_testRun && !std::getenv("QT_QPA_PLATFORM"))
			::setenv("QT_QPA_PLATFORM", "offscreen", 0);
		static int argc = 1;
		static char name[] = "notepad++";
		static char* argv[] = {name, nullptr};
		auto* application = new QApplication(argc, argv);
		QApplication::setApplicationName("Notepad++");
		QGuiApplication::setDesktopFileName("notepad-plus-plus");
		// Quitting is the program's (PostQuitMessage), not the last window's closing.
		application->setQuitOnLastWindowClosed(false);
		application->installEventFilter(new KeyFilter);
		QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, application, [](Qt::ColorScheme)
		{
			windowManagerAppearanceChanged();
		});
		QObject::connect(application, &QGuiApplication::applicationStateChanged, application, [](Qt::ApplicationState state)
		{
			windowManagerApplicationActivated(state == Qt::ApplicationActive);
		});
		scheduleSnapshot();
		scheduleCommands();
	}

	void finishLaunching()
	{
		// Files come on the command line; a second launch hands its files over with J4's desktop step.
	}

	std::vector<std::string> takeLaunchFiles()
	{
		return {};
	}

	void setLaunched()
	{
		g_launched = true;
	}

	// --- Views ---------------------------------------------------------------------------------------

	NativeView* createView(ViewOwner* owner, ViewKind kind, NativeView* parent, const Rect& frame, const TopLevelStyle& style)
	{
		auto* view = new NativeView;
		view->_owner = owner;
		view->_kind = kind;
		view->_style = style;
		if (kind == ViewKind::child)
		{
			auto* surface = new SurfaceWidget(view, parent && parent->_widget ? parent->_widget.data() : nullptr);
			surface->setGeometry(toQRect(frame));
			surface->hide();
			// New child windows start at the bottom of the z-order, as on Windows.
			surface->lower();
			view->_surface = surface;
			view->_widget = surface;
		}
		else
			makeWindow(view, kind, parent, frame, style, nullptr);
		view->_ready = true;
		return view;
	}

	void destroyView(NativeView* view)
	{
		if (!view)
			return;
		if (g_capture == view)
			releaseCapture();
		if (view->_window)
			retire(view->_window);
		else
			retire(view->_widget);
		delete view;
	}

	void setFrame(NativeView* view, const Rect& frame)
	{
		if (!view)
			return;
		if (view->_window)
		{
			if (!view->_style._resizable && view->_kind == ViewKind::topLevel)
			{
				view->_window->setMinimumSize(0, 0);
				view->_window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			}
			view->_window->setGeometry(toQRect(frame));
			if (!view->_style._resizable && view->_kind == ViewKind::topLevel)
				view->_window->setFixedSize(view->_window->size());
		}
		else if (view->_widget)
			view->_widget->setGeometry(toQRect(frame));
	}

	void setVisible(NativeView* view, bool visible, bool activate)
	{
		if (!view)
			return;
		if (view->_window)
		{
			if (!visible)
			{
				view->_window->hide();
				return;
			}
			view->_window->show();
			if (activate && !g_testRun)
			{
				view->_window->raise();
				view->_window->activateWindow();
			}
		}
		else if (view->_widget)
			view->_widget->setVisible(visible);
	}

	void setTitle(NativeView* view, const std::u16string& title)
	{
		if (view && view->_window)
			view->_window->setWindowTitle(toQString(title));
	}

	void setEnabled(NativeView* view, bool enabled)
	{
		// The window manager keeps disabled custom views from input; native controls are told.
		if (view && view->_adopted && view->_widget)
			view->_widget->setEnabled(enabled);
	}

	void setParent(NativeView* view, NativeView* newParent, const Rect& frame, const TopLevelStyle& style, bool visible)
	{
		if (!view || !view->_widget)
			return;
		if (newParent && newParent->_widget)
		{
			// Into another view: a top level gives its window up.
			QPointer<QWidget> oldWindow = view->_window;
			view->_widget->setParent(newParent->_widget);
			view->_widget->raise();
			view->_widget->setVisible(visible);
			if (oldWindow)
			{
				view->_window = nullptr;
				view->_layout = nullptr;
				view->_menuBar = nullptr;
				view->_kind = ViewKind::child;
				retire(oldWindow);
			}
			return;
		}
		if (view->_window)
			return; // already a top level
		// A child becoming a top level: a window of its own, around it.
		view->_kind = ViewKind::topLevel;
		view->_style = style;
		makeWindow(view, ViewKind::topLevel, nullptr, frame, style, view->_widget);
		if (visible)
			view->_window->show();
	}

	void orderView(NativeView* view, bool front, NativeView* above)
	{
		if (!view)
			return;
		if (view->_window)
		{
			if (front)
				view->_window->raise();
			else
				view->_window->lower();
			return;
		}
		if (!view->_widget)
			return;
		if (above && above->_widget && !above->_window)
			view->_widget->stackUnder(above->_widget);
		else if (front)
			view->_widget->raise();
		else
			view->_widget->lower();
	}

	Rect clientRectOnScreen(NativeView* view)
	{
		if (!view || !view->_widget)
			return {};
		const QPoint origin = view->_widget->mapToGlobal(QPoint(0, 0));
		return {origin.x(), origin.y(), origin.x() + view->_widget->width(), origin.y() + view->_widget->height()};
	}

	Rect windowRectOnScreen(NativeView* view)
	{
		if (view && view->_window)
			return toRect(view->_window->geometry());
		return clientRectOnScreen(view);
	}

	Rect windowRectForClient(const Rect& clientOnScreen, const TopLevelStyle&)
	{
		// The desktop draws the title bar and border outside the window; a menu bar takes from the client
		// area, as on Windows.
		return clientOnScreen;
	}

	void invalidate(NativeView* view, const Rect* rect)
	{
		if (!view || !view->_widget)
			return;
		if (rect)
			view->_widget->update(toQRect(*rect));
		else
			view->_widget->update();
	}

	void displayNow(NativeView* view)
	{
		if (view && view->_widget && view->_widget->isVisible())
			view->_widget->repaint();
	}

	void makeKeyAndFocus(NativeView* view)
	{
		if (!view || g_testRun)
			return;
		QWidget* window = view->_window ? view->_window.data() : (view->_widget ? view->_widget->window() : nullptr);
		if (window && window->isVisible() && !window->isActiveWindow())
		{
			window->raise();
			window->activateWindow();
		}
	}

	bool isMinimized(NativeView* view)
	{
		return view && view->_window && view->_window->isMinimized();
	}

	bool isMaximized(NativeView* view)
	{
		return view && view->_window && view->_window->isMaximized() && !view->_window->isMinimized();
	}

	void minimize(NativeView* view)
	{
		if (view && view->_window)
			view->_window->showMinimized();
	}

	void maximize(NativeView* view)
	{
		if (view && view->_window && !view->_window->isMaximized())
			view->_window->showMaximized();
	}

	void restore(NativeView* view)
	{
		if (view && view->_window)
			view->_window->showNormal();
	}

	void* surfaceContext(NativeView* view)
	{
		return view && view->_surface ? view->_surface->surface() : nullptr;
	}

	void setSeeThrough(NativeView* view, bool seeThrough)
	{
		if (!view || !view->_surface)
			return;
		view->_surface->_seeThrough = seeThrough;
		// Clicks go through it to the views under it, as HTTRANSPARENT does; under it shows through.
		view->_surface->setAttribute(Qt::WA_TransparentForMouseEvents, seeThrough);
		view->_surface->setAttribute(Qt::WA_OpaquePaintEvent, !seeThrough);
	}

	void setScrollBar(NativeView* view, bool vertical, const ScrollBarState& state)
	{
		if (!view || !view->_surface)
			return;
		SurfaceWidget* surface = view->_surface;
		const int index = vertical ? 0 : 1;
		surface->_scrollStates[index] = state;
		RuntimeScrollBar*& bar = surface->_scrollBars[index];
		if (!bar)
		{
			if (!state._visible)
				return;
			bar = new RuntimeScrollBar(vertical ? Qt::Vertical : Qt::Horizontal, surface, view);
		}
		const double proportion = std::clamp(state._proportion, 0.0, 1.0);
		bar->blockSignals(true);
		bar->setPageStep(proportion >= 1 ? scrollRange : static_cast<int>(std::lround(scrollRange * proportion / (1 - proportion))));
		bar->setValue(static_cast<int>(std::lround(std::clamp(state._value, 0.0, 1.0) * scrollRange)));
		bar->blockSignals(false);
		bar->setEnabled(state._enabled && proportion < 1);
		surface->layoutScrollBars();
	}

	void setApplicationAppearance(Appearance)
	{
		// Qt follows the desktop's light or dark style for its own parts; Notepad++ draws its own.
	}

	void setWindowAppearance(NativeView*, Appearance)
	{
	}

	int scrollBarThickness()
	{
		return haveApplication() ? QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 15;
	}

	void clearSurface(NativeView* view, const Rect& rect)
	{
		if (view && view->_surface)
			view->_surface->clearSurface(toQRect(rect));
	}

	NativeView* adoptView(ViewOwner* owner, NativeView* parent, void* native, const Rect& frame)
	{
		auto* widget = static_cast<QWidget*>(native);
		auto* view = new NativeView;
		view->_owner = owner;
		view->_kind = ViewKind::child;
		view->_adopted = true;
		view->_widget = widget;
		widget->setParent(parent && parent->_widget ? parent->_widget.data() : nullptr);
		widget->setGeometry(toQRect(frame));
		widget->hide();
		widget->lower();
		view->_ready = true;
		return view;
	}

	void* nativeView(NativeView* view)
	{
		return view ? view->_widget.data() : nullptr;
	}

	// --- Screen and pointer -----------------------------------------------------------------------------

	Rect mainScreenRect()
	{
		QScreen* screen = haveApplication() ? QGuiApplication::primaryScreen() : nullptr;
		return screen ? toRect(screen->geometry()) : Rect{0, 0, 1920, 1080};
	}

	Rect mainScreenWorkArea()
	{
		QScreen* screen = haveApplication() ? QGuiApplication::primaryScreen() : nullptr;
		return screen ? toRect(screen->availableGeometry()) : Rect{0, 0, 1920, 1080};
	}

	Point cursorPosition()
	{
		if (!haveApplication())
			return {};
		const QPoint position = QCursor::pos();
		return {position.x(), position.y()};
	}

	double backingScale()
	{
		QScreen* screen = haveApplication() ? QGuiApplication::primaryScreen() : nullptr;
		return screen ? screen->devicePixelRatio() : 1.0;
	}

	void setAcceptsFiles(NativeView* view, bool accepts)
	{
		if (view && view->_surface)
			view->_surface->setAcceptDrops(accepts);
	}

	// --- Hot keys ---------------------------------------------------------------------------------------

	// Linux has no system-wide hot keys a program can take on every desktop (Wayland leaves them to the
	// desktop's shortcuts portal): none is registered. A test run gets a token, as on the Mac, where its
	// keys are never registered either.
	unsigned registerHotKey(unsigned char, unsigned, std::function<void()>)
	{
		static unsigned nextToken = 1;
		return g_testRun ? nextToken++ : 0;
	}

	void unregisterHotKey(unsigned)
	{
	}

	// --- Capture and cursors -----------------------------------------------------------------------------

	void setCapture(NativeView* view)
	{
		if (!view || !view->_widget || !view->_widget->isVisible())
			return;
		view->_widget->grabMouse();
		g_capture = view;
	}

	void releaseCapture()
	{
		if (g_capture && g_capture->_widget)
			g_capture->_widget->releaseMouse();
		else if (QWidget* grabber = haveApplication() ? QWidget::mouseGrabber() : nullptr)
			grabber->releaseMouse();
		g_capture = nullptr;
	}

	void setCursor(CursorShape shape)
	{
		g_cursor = qtCursor(shape);
		g_cursorHidden = false;
		applyCursor();
	}

	void hideCursor(bool hide)
	{
		g_cursorHidden = hide;
		applyCursor();
	}

	// --- Event loop and timers -------------------------------------------------------------------------

	void runEventLoopOnce(int timeoutMilliseconds)
	{
		if (!QCoreApplication::instance())
		{
			std::unique_lock lock{g_wakeMutex};
			const auto woken = [] { return g_woken; };
			if (timeoutMilliseconds < 0)
				g_wake.wait(lock, woken);
			else if (timeoutMilliseconds > 0)
				g_wake.wait_for(lock, std::chrono::milliseconds{timeoutMilliseconds}, woken);
			g_woken = false;
			return;
		}
		if (timeoutMilliseconds == 0)
			QCoreApplication::processEvents(QEventLoop::AllEvents);
		else
		{
			// Waits for an event (or the timeout's), then handles what has come.
			QTimer timeout;
			if (timeoutMilliseconds > 0)
			{
				timeout.setSingleShot(true);
				timeout.start(timeoutMilliseconds);
			}
			QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
		}
		// Qt deletes what deleteLater left (menus refilled, a menu bar's right end replaced) when its own
		// event loop comes back round; the program's loop is this one, so it is done here, by Qt's rules
		// for what is safe to delete (nothing still being delivered an event). Menu commands are posted,
		// so no menu is in use when its replacement comes.
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

	void wakeEventLoop()
	{
		{
			std::lock_guard lock{g_wakeMutex};
			g_woken = true;
		}
		g_wake.notify_all();
		if (QCoreApplication* application = QCoreApplication::instance())
		{
			if (QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance(application->thread()))
				dispatcher->wakeUp();
		}
	}

	void* startTimer(unsigned milliseconds, TimerCallback callback, void* context, uintptr_t id)
	{
		auto* timer = new QTimer;
		timer->setTimerType(Qt::PreciseTimer);
		QObject::connect(timer, &QTimer::timeout, [callback, context, id] { callback(context, id); });
		timer->start(static_cast<int>(milliseconds));
		return timer;
	}

	void stopTimer(void* timer)
	{
		if (auto* qtTimer = static_cast<QTimer*>(timer))
		{
			qtTimer->stop();
			qtTimer->deleteLater(); // it may be running its own callback
		}
	}

	// --- Keys --------------------------------------------------------------------------------------------

	void retainNativeEvent(void* nativeEvent)
	{
		if (auto* held = static_cast<HeldKey*>(nativeEvent))
			++held->_references;
	}

	void releaseNativeEvent(void* nativeEvent)
	{
		if (auto* held = static_cast<HeldKey*>(nativeEvent); held && --held->_references == 0)
			delete held;
	}

	void forwardKey(NativeView* view, void* nativeEvent)
	{
		auto* held = static_cast<HeldKey*>(nativeEvent);
		if (!view || !view->_widget || !held || !held->_event)
			return;
		QWidget* target = view->_widget->focusProxy() ? view->_widget->focusProxy() : view->_widget.data();
		std::unique_ptr<QKeyEvent> copy(static_cast<QKeyEvent*>(held->_event->clone()));
		t_forwardingKey = true;
		QCoreApplication::sendEvent(target, copy.get());
		t_forwardingKey = false;
	}

	TextInputResult interpretKey(NativeView* view, void*)
	{
		// Qt's input method has already taken the keys it composes with (they never reach the window
		// manager); its text comes through the view's input method events. The view that edits text
		// gets Qt's focus, so that what the input method composes next comes to it.
		if (view && view->_widget && !view->_widget->hasFocus())
			view->_widget->setFocus(Qt::OtherFocusReason);
		return {};
	}

	void testComposition(NativeView* view, const std::u16string& composing, const std::u16string& committed)
	{
		if (!view || !view->_owner)
			return;
		view->_owner->nativeComposition(composing, composing.size());
		if (!committed.empty())
		{
			view->_owner->nativeComposition({}, 0);
			view->_owner->nativeInsertText(committed);
		}
	}

	void focusNativeView(NativeView* view)
	{
		if (view && view->_widget)
			view->_widget->setFocus(Qt::OtherFocusReason);
	}

	// --- Menu bars (MenuBridge.cpp) ----------------------------------------------------------------------

	namespace
	{
		// Windows' menu bar is flat. Fusion draws a line along a menu bar's bottom, which at a fractional
		// scale lands across pixel rows and breaks into double lines under the menu titles.
		class FlatMenuBarStyle : public QProxyStyle
		{
		public:
			void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override
			{
				if (element == CE_MenuBarEmptyArea)
				{
					painter->fillRect(option->rect, option->palette.window());
					return;
				}
				QProxyStyle::drawControl(element, option, painter, widget);
				// Each menu title draws its piece of the line too, along its bottom rows, unless it is open.
				if (element == CE_MenuBarItem && !((option->state & State_Selected) && (option->state & State_Sunken)))
					painter->fillRect(QRect(option->rect.left(), option->rect.bottom() - 1, option->rect.width(), 2), option->palette.window());
			}

			void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override
			{
				if (element == PE_PanelMenuBar)
					return;
				QProxyStyle::drawPrimitive(element, option, painter, widget);
			}
		};
	}

	void styleMenuBar(QMenuBar* bar)
	{
		// One for every menu bar, kept for the program's life (a widget does not own its style).
		static auto* style = new FlatMenuBarStyle;
		bar->setStyle(style);
	}

	QMenuBar* menuBarFor(NativeView* view)
	{
		if (!view || !view->_window || !view->_layout)
			return nullptr;
		if (!view->_menuBar)
		{
			auto* bar = new QMenuBar(view->_window);
			bar->setNativeMenuBar(false); // in the window, as Windows has it
			styleMenuBar(bar);
			view->_layout->insertWidget(0, bar);
			view->_menuBar = bar;
		}
		return view->_menuBar;
	}

	void removeMenuBar(NativeView* view)
	{
		if (!view || !view->_menuBar)
			return;
		QMenuBar* bar = view->_menuBar;
		view->_menuBar = nullptr;
		if (view->_layout)
			view->_layout->removeWidget(bar);
		bar->hide();
		bar->deleteLater();
	}
}
