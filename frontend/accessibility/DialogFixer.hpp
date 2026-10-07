#pragma once

#include <QObject>

class QWidget;

namespace OBS::Accessibility {

/* Application-wide event filter that fills in accessibility information
 * missing from windows when they are shown, so existing dialogs (Settings,
 * Advanced Audio Properties, plugin dialogs) do not have to be edited one by
 * one:
 * - buttons that only show an icon get their tooltip as name
 * - controls in form layout rows get the row's label as name when Qt cannot
 *   associate the label with them
 * - names that are untranslated locale keys are translated */
class DialogFixer : public QObject {
	Q_OBJECT

public:
	explicit DialogFixer(QObject *parent = nullptr);

	static void FixWindow(QWidget *window);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace OBS::Accessibility
