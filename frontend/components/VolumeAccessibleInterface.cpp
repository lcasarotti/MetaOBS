#include "VolumeAccessibleInterface.hpp"

#include <components/VolumeSlider.hpp>

VolumeAccessibleInterface::VolumeAccessibleInterface(QWidget *w) : QAccessibleWidget(w) {}

VolumeSlider *VolumeAccessibleInterface::slider() const
{
	return qobject_cast<VolumeSlider *>(object());
}

QString VolumeAccessibleInterface::text(QAccessible::Text t) const
{
	if (slider()->isVisible()) {
		switch (t) {
		case QAccessible::Text::Value: {
			float db = obs_fader_get_db(slider()->fad);
			return db < -96.0f ? QString("-inf dB") : QString::number(db, 'f', 1).append(" dB");
		}
		default:
			break;
		}
	}
	return QAccessibleWidget::text(t);
}

/* The value interface works in slider positions, like the minimum, maximum
 * and step size, so screen readers can move the slider. The decibel value is
 * exposed as the Value text. */
QVariant VolumeAccessibleInterface::currentValue() const
{
	return slider()->value();
}

void *VolumeAccessibleInterface::interface_cast(QAccessible::InterfaceType type)
{
	if (type == QAccessible::ValueInterface) {
		return static_cast<QAccessibleValueInterface *>(this);
	}
	return QAccessibleWidget::interface_cast(type);
}

void VolumeAccessibleInterface::setCurrentValue(const QVariant &value)
{
	slider()->setValue(value.toInt());
}

QVariant VolumeAccessibleInterface::maximumValue() const
{
	return slider()->maximum();
}

QVariant VolumeAccessibleInterface::minimumValue() const
{
	return slider()->minimum();
}

QVariant VolumeAccessibleInterface::minimumStepSize() const
{
	return slider()->singleStep();
}

QAccessible::Role VolumeAccessibleInterface::role() const
{
	return QAccessible::Role::Slider;
}
