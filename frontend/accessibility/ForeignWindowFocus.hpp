#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace OBS::Accessibility {

/* Some plugins show a user interface that is not written with Qt inside a
 * native Qt widget: atkAudio PluginHost, for instance, puts the window of its
 * JUCE editor inside a dock. Qt gives keyboard focus to the widget but never to
 * the window inside it, so those controls cannot be reached with Tab and
 * screen readers are not told when they change. This application-wide filter
 * (Windows only):
 * - moves Windows focus into such a window when its Qt widget gets focus
 * - moves focus back to the next or previous Qt control on Ctrl+Tab or
 *   Ctrl+Shift+Tab, since the plugin interface keeps Tab for itself */
class ForeignWindowFocus : public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT

public:
	explicit ForeignWindowFocus(QObject *parent = nullptr);
	~ForeignWindowFocus() override;

	bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace OBS::Accessibility
