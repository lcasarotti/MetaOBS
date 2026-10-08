#include "TransitionDialogs.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstring>
#include <string>

namespace OBS::Accessibility {

namespace {
/* The same range as the main window's duration spin boxes. */
constexpr int minimumDurationMs = 50;
constexpr int maximumDurationMs = 20000;
constexpr int durationStepMs = 50;

QSpinBox *CreateDurationSpinBox(QWidget *parent, int value)
{
	QSpinBox *duration = new QSpinBox(parent);
	duration->setRange(minimumDurationMs, maximumDurationMs);
	duration->setSingleStep(durationStepMs);
	duration->setSuffix(" ms");
	duration->setValue(value);
	return duration;
}

struct TransitionForm {
	QDialog *dialog;
	QFormLayout *form;
	QComboBox *transition;
	QSpinBox *duration;
	QDialogButtonBox *buttons;
};

TransitionForm CreateForm(QWidget *parent, const QString &title, int durationMs)
{
	TransitionForm result;
	result.dialog = new QDialog(parent);
	result.dialog->setAttribute(Qt::WA_DeleteOnClose);
	result.dialog->setWindowTitle(title);

	QVBoxLayout *layout = new QVBoxLayout(result.dialog);
	result.form = new QFormLayout();
	layout->addLayout(result.form);

	result.transition = new QComboBox(result.dialog);
	result.form->addRow(QTStr("Transition"), result.transition);

	result.duration = CreateDurationSpinBox(result.dialog, durationMs);
	result.form->addRow(QTStr("Basic.TransitionDuration"), result.duration);

	result.buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, result.dialog);
	layout->addWidget(result.buttons);
	QObject::connect(result.buttons, &QDialogButtonBox::accepted, result.dialog, &QDialog::accept);
	QObject::connect(result.buttons, &QDialogButtonBox::rejected, result.dialog, &QDialog::reject);

	return result;
}
} // namespace

void EditVisibilityTransition(OBSBasic *main, obs_sceneitem_t *item, bool visible, QWidget *parent)
{
	obs_source_t *itemSource = obs_sceneitem_get_source(item);
	obs_source_t *current = obs_sceneitem_get_transition(item, visible);
	const char *currentId = current ? obs_source_get_id(current) : nullptr;

	int durationMs = static_cast<int>(obs_sceneitem_get_transition_duration(item, visible));
	if (durationMs <= 0) {
		durationMs = obs_frontend_get_transition_duration();
	}

	QString title = QTStr(visible ? "ShowTransition" : "HideTransition") + " - " +
			QT_UTF8(obs_source_get_name(itemSource));
	TransitionForm form = CreateForm(parent, title, durationMs);

	form.transition->addItem(QTStr("None"), QString());
	size_t index = 0;
	const char *id = nullptr;
	while (obs_enum_transition_types(index++, &id)) {
		form.transition->addItem(QT_UTF8(obs_source_get_display_name(id)), QT_UTF8(id));
		if (currentId && strcmp(id, currentId) == 0) {
			form.transition->setCurrentIndex(form.transition->count() - 1);
		}
	}

	/* Some transitions, such as Stinger, have settings of their own. */
	QPushButton *properties = form.buttons->addButton(QTStr("Properties"), QDialogButtonBox::ActionRole);
	auto updateProperties = [form, properties]() {
		QString selectedId = form.transition->currentData().toString();
		properties->setEnabled(!selectedId.isEmpty() && obs_is_source_configurable(QT_TO_UTF8(selectedId)));
	};
	updateProperties();
	QObject::connect(form.transition, &QComboBox::currentIndexChanged, form.dialog, updateProperties);

	std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(obs_sceneitem_get_scene(item)));
	int64_t itemId = obs_sceneitem_get_id(item);
	auto findItem = [sceneUuid, itemId]() -> obs_sceneitem_t * {
		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.c_str());
		obs_scene_t *scene = obs_group_or_scene_from_source(sceneSource);
		return scene ? obs_scene_find_sceneitem_by_id(scene, itemId) : nullptr;
	};

	/* Applies the dialog's choice to the item and returns its transition,
	 * if any. */
	auto apply = [form, findItem, visible]() -> obs_source_t * {
		obs_sceneitem_t *target = findItem();
		if (!target) {
			return nullptr;
		}

		QString selectedId = form.transition->currentData().toString();
		if (selectedId.isEmpty()) {
			obs_sceneitem_set_transition(target, visible, nullptr);
			obs_sceneitem_set_transition_duration(target, visible, 0);
			return nullptr;
		}

		obs_source_t *transition = obs_sceneitem_get_transition(target, visible);
		if (!transition || selectedId != obs_source_get_id(transition)) {
			QString name = QT_UTF8(obs_source_get_name(obs_sceneitem_get_source(target))) + " " +
				       QTStr(visible ? "ShowTransition" : "HideTransition");
			OBSSourceAutoRelease created =
				obs_source_create_private(QT_TO_UTF8(selectedId), QT_TO_UTF8(name), nullptr);
			obs_sceneitem_set_transition(target, visible, created);
			transition = created;
		}

		obs_sceneitem_set_transition_duration(target, visible, form.duration->value());
		return transition;
	};

	OBSDataAutoRelease before = obs_sceneitem_transition_save(item, visible);
	std::string undoData = obs_data_get_json(before);

	auto addUndo = [main, findItem, visible, undoData, itemSource = OBSSource(itemSource)]() {
		obs_sceneitem_t *target = findItem();
		if (!target) {
			return;
		}

		OBSDataAutoRelease after = obs_sceneitem_transition_save(target, visible);
		std::string redoData = obs_data_get_json(after);
		if (redoData == undoData) {
			return;
		}

		auto undoRedo = [findItem, visible](const std::string &data) {
			if (obs_sceneitem_t *sceneItem = findItem()) {
				OBSDataAutoRelease saved = obs_data_create_from_json(data.c_str());
				obs_sceneitem_transition_load(sceneItem, saved, visible);
			}
		};

		main->undo_s.add_action(QTStr(visible ? "Undo.ShowTransition" : "Undo.HideTransition")
						.arg(obs_source_get_name(itemSource)),
					undoRedo, undoRedo, undoData, redoData);
	};

	QObject::connect(properties, &QPushButton::clicked, form.dialog, [main, apply]() {
		if (obs_source_t *transition = apply()) {
			main->CreatePropertiesWindow(transition);
		}
	});
	QObject::connect(form.dialog, &QDialog::accepted, form.dialog, [apply, addUndo]() {
		apply();
		addUndo();
	});
	/* Settings opened with the Properties button were already applied, so
	 * Cancel restores the previous state. */
	QObject::connect(form.dialog, &QDialog::rejected, form.dialog, [findItem, visible, undoData]() {
		if (obs_sceneitem_t *target = findItem()) {
			OBSDataAutoRelease saved = obs_data_create_from_json(undoData.c_str());
			obs_sceneitem_transition_load(target, saved, visible);
		}
	});

	form.dialog->open();
}

void EditSceneTransitionOverride(obs_source_t *sceneSource, QWidget *parent)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(sceneSource);
	obs_data_set_default_int(settings, "transition_duration", 300);

	QString currentName = QT_UTF8(obs_data_get_string(settings, "transition"));
	int durationMs = static_cast<int>(obs_data_get_int(settings, "transition_duration"));

	QString title = QTStr("TransitionOverride") + " - " + QT_UTF8(obs_source_get_name(sceneSource));
	TransitionForm form = CreateForm(parent, title, durationMs);

	form.transition->addItem(QTStr("None"), QString());
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		QString name = QT_UTF8(obs_source_get_name(transitions.sources.array[i]));
		form.transition->addItem(name, name);
		if (name == currentName) {
			form.transition->setCurrentIndex(form.transition->count() - 1);
		}
	}
	obs_frontend_source_list_free(&transitions);

	OBSWeakSource weakScene = OBSGetWeakRef(sceneSource);
	QObject::connect(form.dialog, &QDialog::accepted, form.dialog, [form, weakScene]() {
		OBSSource scene = OBSGetStrongRef(weakScene);
		if (!scene) {
			return;
		}

		OBSDataAutoRelease sceneSettings = obs_source_get_private_settings(scene);
		obs_data_set_string(sceneSettings, "transition", QT_TO_UTF8(form.transition->currentData().toString()));
		obs_data_set_int(sceneSettings, "transition_duration", form.duration->value());
	});

	form.dialog->open();
}

} // namespace OBS::Accessibility
