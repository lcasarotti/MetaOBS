#include "ForeignWindowFocus.hpp"

#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <cwchar>

#include <windows.h>

#include "moc_ForeignWindowFocus.cpp"

namespace OBS::Accessibility {

namespace {
bool IsQtWindow(HWND hwnd)
{
	wchar_t className[64] = {};
	GetClassNameW(hwnd, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
	return std::wcsncmp(className, L"Qt", 2) == 0;
}

/* The first window that something other than Qt created inside a native
 * widget. */
HWND FindForeignChild(const QWidget *widget)
{
	HWND parent = reinterpret_cast<HWND>(widget->internalWinId());
	if (!parent) {
		return nullptr;
	}

	for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		if (IsWindowVisible(child) && IsWindowEnabled(child) && !IsQtWindow(child)) {
			return child;
		}
	}
	return nullptr;
}

bool HasFocusInside(HWND hwnd)
{
	HWND focus = GetFocus();
	return focus && (focus == hwnd || IsChild(hwnd, focus));
}

/* The Qt widget that contains a window Qt did not create. Owners are not
 * followed, so a separate plugin window is never treated as embedded. */
QWidget *FindHostWidget(HWND hwnd)
{
	HWND desktop = GetDesktopWindow();
	for (HWND parent = GetAncestor(hwnd, GA_PARENT); parent && parent != desktop;
	     parent = GetAncestor(parent, GA_PARENT)) {
		QWidget *widget = QWidget::find(reinterpret_cast<WId>(parent));
		if (widget && !widget->isWindow()) {
			return widget;
		}
	}
	return nullptr;
}

bool CanTakeTabFocus(const QWidget *widget, const QWidget *host)
{
	return widget->window() == host->window() && widget->isVisible() && widget->isEnabled() &&
	       (widget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus && !host->isAncestorOf(widget);
}

/* Returns false when the host is the only control in its window, so the key
 * still reaches the plugin interface. */
bool LeaveForeignWindow(QWidget *host, bool backward)
{
	QWidget *next = host;
	do {
		next = backward ? next->previousInFocusChain() : next->nextInFocusChain();
	} while (next != host && !CanTakeTabFocus(next, host));

	if (next == host) {
		return false;
	}

	next->setFocus(backward ? Qt::BacktabFocusReason : Qt::TabFocusReason);
	SetFocus(reinterpret_cast<HWND>(host->window()->winId()));
	return true;
}
} // namespace

ForeignWindowFocus::ForeignWindowFocus(QObject *parent) : QObject(parent)
{
	qApp->installNativeEventFilter(this);
}

ForeignWindowFocus::~ForeignWindowFocus()
{
	qApp->removeNativeEventFilter(this);
}

bool ForeignWindowFocus::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() != QEvent::FocusIn || !watched->isWidgetType()) {
		return false;
	}

	QWidget *widget = static_cast<QWidget *>(watched);
	if (widget->isWindow() || !widget->internalWinId()) {
		return false;
	}

	/* Wait until Qt has finished moving focus, since activating the window
	 * can send focus events of its own. */
	QPointer<QWidget> host = widget;
	QTimer::singleShot(0, this, [host]() {
		if (!host || !host->hasFocus()) {
			return;
		}

		HWND child = FindForeignChild(host);
		if (child && !HasFocusInside(child)) {
			SetFocus(child);
		}
	});

	return false;
}

bool ForeignWindowFocus::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
	if (eventType != "windows_generic_MSG") {
		return false;
	}

	const MSG *msg = static_cast<const MSG *>(message);
	if (msg->message != WM_KEYDOWN || msg->wParam != VK_TAB || !(GetKeyState(VK_CONTROL) & 0x8000) ||
	    IsQtWindow(msg->hwnd)) {
		return false;
	}

	QWidget *host = FindHostWidget(msg->hwnd);
	if (!host) {
		return false;
	}

	bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	return LeaveForeignWindow(host, backward);
}

} // namespace OBS::Accessibility
