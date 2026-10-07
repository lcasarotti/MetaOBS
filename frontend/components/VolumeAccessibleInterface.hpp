#pragma once

#include <QAccessibleWidget>

class VolumeSlider;

class VolumeAccessibleInterface : public QAccessibleWidget, public QAccessibleValueInterface {

public:
	VolumeAccessibleInterface(QWidget *w);

	QVariant currentValue() const override;
	void setCurrentValue(const QVariant &value) override;

	QVariant maximumValue() const override;
	QVariant minimumValue() const override;

	QVariant minimumStepSize() const override;

	void *interface_cast(QAccessible::InterfaceType type) override;

private:
	VolumeSlider *slider() const;

protected:
	virtual QAccessible::Role role() const override;
	virtual QString text(QAccessible::Text t) const override;
};
