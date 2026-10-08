#include "DialogFixer.hpp"

#include <OBSApp.hpp>

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QWidget>

#include "moc_DialogFixer.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int focusRepeatDelayMs = 150;

QString ToPlainText(QString text)
{
	if (Qt::mightBeRichText(text)) {
		text = QTextDocumentFragment::fromHtml(text).toPlainText();
	}
	text.remove(QRegularExpression("&(?!&)"));
	return text.simplified();
}

bool LooksLikeLocaleKey(const QString &text)
{
	static const QRegularExpression localeKey("^[A-Z][A-Za-z0-9]*(\\.[A-Z0-9][A-Za-z0-9]*)+$");
	return localeKey.match(text).hasMatch();
}

bool IsFocusableControl(const QWidget *widget)
{
	if ((widget->focusPolicy() & Qt::TabFocus) != Qt::TabFocus) {
		return false;
	}

	const QWidget *parent = widget->parentWidget();
	return !qobject_cast<const QAbstractSpinBox *>(parent) && !qobject_cast<const QComboBox *>(parent);
}

void CollectWidgets(QLayoutItem *item, QList<QWidget *> &widgets)
{
	if (!item) {
		return;
	}

	if (QWidget *widget = item->widget()) {
		widgets.append(widget);
		widgets.append(widget->findChildren<QWidget *>());
	} else if (QLayout *layout = item->layout()) {
		for (int i = 0; i < layout->count(); i++) {
			CollectWidgets(layout->itemAt(i), widgets);
		}
	}
}

QString GetLabelText(QLayoutItem *labelItem)
{
	QWidget *labelWidget = labelItem ? labelItem->widget() : nullptr;
	if (QLabel *label = qobject_cast<QLabel *>(labelWidget)) {
		return ToPlainText(label->text());
	}
	return {};
}

void NameFormRows(QFormLayout *layout)
{
	for (int row = 0; row < layout->rowCount(); row++) {
		QString labelText = GetLabelText(layout->itemAt(row, QFormLayout::LabelRole));
		if (labelText.isEmpty()) {
			continue;
		}

		QList<QWidget *> widgets;
		CollectWidgets(layout->itemAt(row, QFormLayout::FieldRole), widgets);

		for (QWidget *widget : widgets) {
			if (!IsFocusableControl(widget) || !widget->accessibleName().isEmpty()) {
				continue;
			}

			/* Buttons and checkboxes with text already have a name. */
			QAbstractButton *button = qobject_cast<QAbstractButton *>(widget);
			if (button && !button->text().isEmpty()) {
				if (widget->accessibleDescription().isEmpty() && !qobject_cast<QCheckBox *>(widget)) {
					widget->setAccessibleDescription(labelText);
				}
				continue;
			}

			widget->setAccessibleName(labelText);
		}
	}
}

void NameIconButtons(QWidget *window)
{
	for (QAbstractButton *button : window->findChildren<QAbstractButton *>()) {
		if (!button->text().isEmpty() || !button->accessibleName().isEmpty()) {
			continue;
		}

		QString toolTip = ToPlainText(button->toolTip());
		if (!toolTip.isEmpty()) {
			button->setAccessibleName(toolTip);
		}
	}
}

void TranslateLocaleKeys(QWidget *window)
{
	for (QWidget *widget : window->findChildren<QWidget *>()) {
		QString name = widget->accessibleName();
		if (LooksLikeLocaleKey(name)) {
			widget->setAccessibleName(QTStr(name.toUtf8().constData()));
		}
	}
}
} // namespace

DialogFixer::DialogFixer(QObject *parent) : QObject(parent) {}

void DialogFixer::FixWindow(QWidget *window)
{
	NameIconButtons(window);

	for (QFormLayout *layout : window->findChildren<QFormLayout *>()) {
		NameFormRows(layout);
	}

	TranslateLocaleKeys(window);
}

/* A dialog that gives focus to a control while it is being activated sends
 * the focus event before screen readers have noticed the new window, so they
 * ignore it and the control is not read until focus moves again. Send it again
 * once the dialog is active. */
void DialogFixer::RepeatFocus(QWidget *dialog)
{
	QPointer<QWidget> window = dialog;
	QTimer::singleShot(focusRepeatDelayMs, this, [window]() {
		if (!window || QApplication::activeWindow() != window) {
			return;
		}

		QWidget *focus = QApplication::focusWidget();
		if (focus && window->isAncestorOf(focus)) {
			QAccessibleEvent event(focus, QAccessible::Focus);
			QAccessible::updateAccessibility(&event);
		}
	});
}

bool DialogFixer::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::WindowActivate && qobject_cast<QDialog *>(watched)) {
		RepeatFocus(static_cast<QWidget *>(watched));
		return false;
	}

	if (event->type() != QEvent::Show || !watched->isWidgetType()) {
		return false;
	}

	QWidget *widget = static_cast<QWidget *>(watched);
	Qt::WindowType type = widget->windowType();

	if (!widget->isWindow() || qobject_cast<QMenu *>(widget) || type == Qt::ToolTip || type == Qt::Popup) {
		return false;
	}

	/* Let the window finish building its contents first. */
	QPointer<QWidget> window = widget;
	QTimer::singleShot(0, this, [window]() {
		if (window) {
			FixWindow(window);
		}
	});

	return false;
}

} // namespace OBS::Accessibility
