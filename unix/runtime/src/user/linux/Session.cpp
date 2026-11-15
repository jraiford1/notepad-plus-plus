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


// The desktop's session ending, as Windows' WM_QUERYENDSESSION and WM_ENDSESSION; and its light or dark
// preference changing, as WM_SETTINGCHANGE. GNOME's session
// manager asks the programs registered with it over D-Bus before the session ends: Notepad++ answers
// as it does on Windows (with unsaved documents and no backups it declines and asks to save them, which
// GNOME shows as a program holding up the logout), and saves its session when it ends. A session ended
// by a signal (SIGTERM from systemd or kill, SIGINT, SIGHUP) is a critical end: saved, then quit.

#include "QtViews.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QObject>
#include <QtCore/QSocketNotifier>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusVariant>

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace nppmac::ui
{
	namespace
	{
		const QString sessionService = QStringLiteral("org.gnome.SessionManager");
		const QString clientInterface = QStringLiteral("org.gnome.SessionManager.ClientPrivate");

		// Once: the session ends whatever the program answers.
		void endSessionNow()
		{
			static bool ended = false;
			if (ended)
				return;
			ended = true;
			windowManagerQueryEndSession(true);
			windowManagerEndSession(true);
		}

		// A signal's end: written to a pipe by the handler, read in the event loop.
		int g_signalPipe[2] = {-1, -1};

		void onSignal(int)
		{
			const char byte = 1;
			[[maybe_unused]] const ssize_t written = ::write(g_signalPipe[1], &byte, 1);
		}
	}

	// The program as a client of GNOME's session manager.
	class SessionClient : public QObject
	{
		Q_OBJECT

	public:
		explicit SessionClient(QObject* parent) : QObject(parent)
		{
			QDBusConnection bus = QDBusConnection::sessionBus();
			if (!bus.isConnected())
				return;
			QDBusMessage call = QDBusMessage::createMethodCall(sessionService, "/org/gnome/SessionManager", sessionService, "RegisterClient");
			call << QStringLiteral("notepad-plus-plus") << QString::fromLocal8Bit(qgetenv("DESKTOP_AUTOSTART_ID"));
			const QDBusMessage reply = bus.call(call, QDBus::Block, 3000);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
				return; // no GNOME session: signals end it
			_client = qvariant_cast<QDBusObjectPath>(reply.arguments().constFirst()).path();
			bus.connect(sessionService, _client, clientInterface, "QueryEndSession", this, SLOT(queryEndSession(uint)));
			bus.connect(sessionService, _client, clientInterface, "EndSession", this, SLOT(endSession(uint)));
			bus.connect(sessionService, _client, clientInterface, "Stop", this, SLOT(stop()));
		}

	private slots:
		void queryEndSession(uint flags)
		{
			// Flag 1: a forced logout, which ends whatever the answer.
			const bool agreed = windowManagerQueryEndSession((flags & 1) != 0);
			respond(agreed, agreed ? QString() : QStringLiteral("Unsaved documents"));
		}

		void endSession(uint)
		{
			endSessionNow();
			respond(true, {});
		}

		void stop()
		{
			endSessionNow();
		}

	private:
		QString _client;

		void respond(bool agreed, const QString& reason)
		{
			QDBusMessage call = QDBusMessage::createMethodCall(sessionService, _client, clientInterface, "EndSessionResponse");
			call << agreed << reason;
			QDBusConnection::sessionBus().call(call, QDBus::NoBlock);
		}
	};

	// The desktop's light or dark preference changing (the settings portal's SettingChanged), as
	// Windows' WM_SETTINGCHANGE "ImmersiveColorSet": Qt's colour scheme is the program's, so it does not
	// tell of the desktop's.
	class AppearanceWatcher : public QObject
	{
		Q_OBJECT

	public:
		explicit AppearanceWatcher(QObject* parent) : QObject(parent)
		{
			QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
				"org.freedesktop.portal.Settings", "SettingChanged", this, SLOT(settingChanged(QString, QString, QDBusVariant)));
		}

	private slots:
		void settingChanged(const QString& group, const QString& key, const QDBusVariant&)
		{
			if (group == QLatin1String("org.freedesktop.appearance") && key == QLatin1String("color-scheme"))
				windowManagerAppearanceChanged();
		}
	};

	void startSessionHandling()
	{
		auto* application = QCoreApplication::instance();
		if (!application)
			return;
		// A signal ends the session, run from the event loop.
		if (::pipe2(g_signalPipe, O_CLOEXEC | O_NONBLOCK) == 0)
		{
			auto* notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, application);
			QObject::connect(notifier, &QSocketNotifier::activated, application, []
			{
				char bytes[16];
				while (::read(g_signalPipe[0], bytes, sizeof(bytes)) > 0)
				{
				}
				endSessionNow();
			});
			struct sigaction action{};
			action.sa_handler = onSignal;
			sigemptyset(&action.sa_mask);
			action.sa_flags = SA_RESTART;
			for (const int signal : {SIGTERM, SIGINT, SIGHUP})
				::sigaction(signal, &action, nullptr);
		}
		// A test run is no client of the user's session.
		if (!std::getenv("NPPMAC_TEST_RUN"))
			new SessionClient(application);
		new AppearanceWatcher(application);
	}
}

#include "Session.moc"
