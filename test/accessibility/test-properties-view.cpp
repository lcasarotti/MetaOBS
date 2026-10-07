/* Builds a properties view with one property of each kind and audits what a
 * screen reader receives. */

#include "a11y-test.hpp"

#include <accessibility/A11yAudit.hpp>
#include <properties-view.hpp>

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>

#include <iostream>

using OBS::Accessibility::AuditOptions;
using OBS::Accessibility::AuditWidgetTree;
using OBS::Accessibility::FormatIssues;

namespace {
bool OnButtonClicked(obs_properties_t *, obs_property_t *, void *)
{
	return false;
}

obs_properties_t *CreateTestProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "bool", "Enable feature");
	obs_properties_add_int(props, "int", "Width", 0, 100, 1);
	obs_properties_add_int_slider(props, "int_slider", "Opacity", 0, 100, 1);
	obs_properties_add_float(props, "float", "Speed", 0.0, 10.0, 0.1);
	obs_properties_add_float_slider(props, "float_slider", "Gain", -30.0, 30.0, 0.1);
	obs_properties_add_text(props, "text", "Title", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "password", "Stream key", OBS_TEXT_PASSWORD);
	obs_properties_add_text(props, "multiline", "Notes", OBS_TEXT_MULTILINE);
	obs_properties_add_path(props, "path", "Image file", OBS_PATH_FILE, "Images (*.png)", nullptr);

	obs_property_t *list =
		obs_properties_add_list(props, "list", "Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, "First device", "first");
	obs_property_list_add_string(list, "Second device", "second");

	obs_property_t *radio =
		obs_properties_add_list(props, "radio", "Mode", OBS_COMBO_TYPE_RADIO, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(radio, "Window", "window");
	obs_property_list_add_string(radio, "Display", "display");

	obs_properties_add_color(props, "color", "Text color");
	obs_properties_add_color_alpha(props, "color_alpha", "Background color");
	obs_properties_add_font(props, "font", "Font");
	obs_properties_add_editable_list(props, "editable_list", "Playlist", OBS_EDITABLE_LIST_TYPE_FILES,
					 nullptr, nullptr);
	obs_properties_add_button2(props, "button", "Refresh devices", OnButtonClicked, nullptr);

	obs_property_t *described = obs_properties_add_text(props, "described", "Server", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(described, "Address of the streaming server");

	obs_properties_t *groupProps = obs_properties_create();
	obs_properties_add_int(groupProps, "group_int", "Delay", 0, 1000, 1);
	obs_properties_add_group(props, "group", "Advanced", OBS_GROUP_NORMAL, groupProps);

	obs_properties_t *checkableProps = obs_properties_create();
	obs_properties_add_int(checkableProps, "checkable_int", "Retries", 0, 10, 1);
	obs_properties_add_group(props, "checkable_group", "Reconnect", OBS_GROUP_CHECKABLE, checkableProps);

	return props;
}

OBSPropertiesView *CreateView()
{
	OBSDataAutoRelease settings = obs_data_create();
	OBSPropertiesView *view = new OBSPropertiesView(settings.Get(), "a11y-test", CreateTestProperties);
	view->resize(600, 900);

	/* The view builds its widgets in a queued call. */
	QApplication::processEvents();
	QApplication::processEvents();

	return view;
}
} // namespace

A11Y_TEST(PropertiesViewPassesAudit)
{
	OBSPropertiesView *view = CreateView();

	AuditOptions options;
	/* Translations are not loaded in this test. */
	options.checkUntranslatedKeys = false;

	auto issues = AuditWidgetTree(view, options);
	if (!issues.empty()) {
		std::cout << FormatIssues(issues).toStdString();
	}
	A11Y_CHECK_MESSAGE(issues.empty(), std::to_string(issues.size()) + " accessibility issues");

	delete view;
}

A11Y_TEST(PropertiesViewGroupsUseTheirTitle)
{
	OBSPropertiesView *view = CreateView();

	int groupCount = 0;
	for (QGroupBox *groupBox : view->findChildren<QGroupBox *>()) {
		QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(groupBox);
		A11Y_CHECK(iface);
		if (iface) {
			QString name = iface->text(QAccessible::Name);
			A11Y_CHECK_MESSAGE(name == groupBox->title(),
					   "group \"" + groupBox->title().toStdString() + "\" is named \"" +
						   name.toStdString() + "\"");
		}
		groupCount++;
	}

	A11Y_CHECK(groupCount == 2);

	delete view;
}

/* Sources such as video capture devices update their own settings right after
 * being created, which rebuilds the view while the user is in it. Focus must
 * stay on the same property instead of being lost. */
A11Y_TEST(PropertiesViewKeepsFocusWhenReloaded)
{
	OBSPropertiesView *view = CreateView();
	view->show();

	/* The minimal platform used by the tests does not activate windows on
	 * request, which focus needs. */
	QT_WARNING_PUSH
	QT_WARNING_DISABLE_DEPRECATED
	QApplication::setActiveWindow(view);
	QT_WARNING_POP
	QApplication::processEvents();

	auto findDeviceCombo = [view]() -> QComboBox * {
		for (QComboBox *combo : view->findChildren<QComboBox *>()) {
			if (combo->accessibleName() == "Device") {
				return combo;
			}
		}
		return nullptr;
	};

	QComboBox *focusTarget = findDeviceCombo();
	A11Y_CHECK_MESSAGE(focusTarget, "no combo box named after the \"Device\" property");
	if (!focusTarget) {
		delete view;
		return;
	}

	focusTarget->setFocus(Qt::OtherFocusReason);
	QApplication::processEvents();
	A11Y_CHECK_MESSAGE(QApplication::focusWidget() == focusTarget, "could not focus the test control");

	view->ReloadProperties();
	QApplication::processEvents();
	QApplication::processEvents();

	QWidget *focused = QApplication::focusWidget();
	A11Y_CHECK_MESSAGE(focused, "focus was lost after reloading");
	if (focused) {
		A11Y_CHECK_MESSAGE(focused == findDeviceCombo(), std::string("focus moved to a ") +
									 focused->metaObject()->className());
	}

	delete view;
}

A11Y_TEST(PropertiesViewColorButtonIncludesValue)
{
	OBSPropertiesView *view = CreateView();

	bool found = false;
	for (QPushButton *button : view->findChildren<QPushButton *>()) {
		QString name = button->accessibleName();
		if (name.startsWith("Text color: ")) {
			found = true;
			A11Y_CHECK_MESSAGE(name.contains('#'), "color button is named \"" + name.toStdString() + "\"");
		}
	}

	A11Y_CHECK_MESSAGE(found, "no button named after the \"Text color\" property");

	delete view;
}
