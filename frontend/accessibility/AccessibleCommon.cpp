#include "AccessibleCommon.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>
#include <dialogs/NameDialog.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>

#include <algorithm>
#include <functional>
#include <string>

namespace OBS::Accessibility {

namespace {
bool SelectIfListed(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto targets = static_cast<std::vector<obs_sceneitem_t *> *>(param);
	bool selected = std::find(targets->begin(), targets->end(), item) != targets->end();
	if (obs_sceneitem_selected(item) != selected) {
		obs_sceneitem_select(item, selected);
	}

	/* Groups' items can be selected too. */
	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, SelectIfListed, param);
	}
	return true;
}
} // namespace

void RenameSourceWithUndo(QWidget *parent, OBSBasic *main, obs_source_t *source)
{
	if (!source) {
		return;
	}

	std::string prevName = obs_source_get_name(source);
	std::string name;

	bool accepted = NameDialog::AskForName(parent, QTStr("Accessibility.Rename.Title"),
					       QTStr("Accessibility.Rename.Text"), name, QT_UTF8(prevName.c_str()));
	if (!accepted || name == prevName) {
		return;
	}

	if (name.empty()) {
		OBSMessageBox::warning(parent, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
		return;
	}

	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing) {
		OBSMessageBox::warning(parent, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
		return;
	}

	auto undo = [prevName](const std::string &uuid) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		obs_source_set_name(source, prevName.c_str());
	};
	auto redo = [name](const std::string &uuid) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
		obs_source_set_name(source, name.c_str());
	};

	std::string uuid = obs_source_get_uuid(source);
	main->undo_s.add_action(QTStr("Undo.Rename").arg(name.c_str()), undo, redo, uuid, uuid);
	obs_source_set_name(source, name.c_str());
}

void SetItemVisibleWithUndo(OBSBasic *main, obs_sceneitem_t *item, bool visible)
{
	obs_source_t *sceneSource = obs_scene_get_source(obs_sceneitem_get_scene(item));
	obs_source_t *source = obs_sceneitem_get_source(item);
	int64_t id = obs_sceneitem_get_id(item);
	std::string uuid = obs_source_get_uuid(sceneSource);

	auto undoRedo = [](const std::string &sceneUuid, int64_t itemId, bool value) {
		OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
		obs_scene_t *sceneOrGroup = obs_group_or_scene_from_source(scene);
		obs_sceneitem_t *sceneItem = obs_scene_find_sceneitem_by_id(sceneOrGroup, itemId);
		if (sceneItem) {
			obs_sceneitem_set_visible(sceneItem, value);
		}
	};

	QString actionName = QTStr(visible ? "Undo.ShowSceneItem" : "Undo.HideSceneItem");
	main->undo_s.add_action(actionName.arg(obs_source_get_name(source), obs_source_get_name(sceneSource)),
				std::bind(undoRedo, std::placeholders::_1, id, !visible),
				std::bind(undoRedo, std::placeholders::_1, id, visible), uuid, uuid);

	obs_sceneitem_set_visible(item, visible);

	blog(LOG_INFO, "User set sceneitem '%s' (%li) on scene '%s' to %s", obs_source_get_name(source), (long)id,
	     obs_source_get_name(sceneSource), visible ? "enabled" : "disabled");
}

OBSSceneItem FindSceneItem(const QString &sceneUuid, int64_t itemId)
{
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(QT_TO_UTF8(sceneUuid));
	obs_scene_t *scene = obs_group_or_scene_from_source(sceneSource);
	return scene ? OBSSceneItem(obs_scene_find_sceneitem_by_id(scene, itemId)) : OBSSceneItem();
}

void SelectOnly(obs_scene_t *scene, const std::vector<obs_sceneitem_t *> &items)
{
	std::vector<obs_sceneitem_t *> targets = items;
	obs_scene_enum_items(scene, SelectIfListed, &targets);
}

void Announce(const QString &text)
{
	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(text, Priority::High);
	}
}

void AnnounceRequested(const QString &text)
{
	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(text, Priority::High, true);
	}
}

QAction *CreatePanelAction(QWidget *owner, const QString &text, const QList<QKeySequence> &shortcuts)
{
	QAction *action = new QAction(text, owner);
	if (!shortcuts.isEmpty()) {
		action->setShortcuts(shortcuts);
		action->setShortcutContext(Qt::WidgetShortcut);
		owner->addAction(action);
	}
	return action;
}

QAction *CreateMirrorAction(QAction *target, QObject *parent, const QList<QKeySequence> &reserved)
{
	QAction *mirror = new QAction(parent);
	auto update = [mirror, target, reserved]() {
		QList<QKeySequence> shortcuts = target->shortcuts();
		shortcuts.removeIf([&reserved](const QKeySequence &shortcut) { return reserved.contains(shortcut); });

		mirror->setText(target->text());
		mirror->setShortcuts(shortcuts);
		mirror->setCheckable(target->isCheckable());
		mirror->setChecked(target->isChecked());
		mirror->setEnabled(target->isEnabled());
		mirror->setVisible(target->isVisible());
	};

	update();
	QObject::connect(target, &QAction::changed, mirror, update);
	QObject::connect(mirror, &QAction::triggered, target, &QAction::trigger);
	return mirror;
}

void FillProjectorMenu(QMenu *menu, const char *type, const QString &sourceName)
{
	menu->clear();

	std::string typeName = type;
	std::string name = sourceName.toStdString();
	auto open = [typeName, name](int monitor) {
		obs_frontend_open_projector(typeName.c_str(), monitor, nullptr, name.c_str());
	};

	QList<QString> monitors = OBSBasic::GetProjectorMenuMonitorsFormatted();
	for (int monitor = 0; monitor < monitors.size(); monitor++) {
		QAction *action = menu->addAction(monitors[monitor]);
		QObject::connect(action, &QAction::triggered, menu, [open, monitor]() { open(monitor); });
	}

	menu->addSeparator();
	QAction *window = menu->addAction(QTStr("Projector.Window"));
	QObject::connect(window, &QAction::triggered, menu, [open]() { open(-1); });
}

bool IsHiddenInMixer(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	return obs_data_get_bool(settings, "mixer_hidden");
}

void SetHiddenInMixer(OBSBasic *main, obs_source_t *source, bool hidden)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	obs_data_set_bool(settings, "mixer_hidden", hidden);
	emit main->mixerStatusChanged(QT_UTF8(obs_source_get_uuid(source)));
}

OBSSource DuplicateSceneWithUndo(QWidget *parent, OBSBasic *main, obs_source_t *sceneSource)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		return nullptr;
	}

	/* The same default name as the main window: "Scene 2", "Scene 3"... */
	QString format = QT_UTF8(obs_source_get_name(sceneSource)) + " %1";
	int number = 2;
	QString defaultName = format.arg(number);
	for (;;) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(QT_TO_UTF8(defaultName));
		if (!existing) {
			break;
		}
		defaultName = format.arg(++number);
	}

	std::string name;
	for (;;) {
		if (!NameDialog::AskForName(parent, QTStr("Basic.Main.AddSceneDlg.Title"),
					    QTStr("Basic.Main.AddSceneDlg.Text"), name, defaultName)) {
			return nullptr;
		}

		if (name.empty()) {
			OBSMessageBox::warning(parent, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
			continue;
		}

		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (existing) {
			OBSMessageBox::warning(parent, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
			continue;
		}
		break;
	}

	OBSSceneAutoRelease copy = obs_scene_duplicate(scene, name.c_str(), OBS_SCENE_DUP_REFS);
	OBSSource copySource = obs_scene_get_source(copy);

	auto undo = [](const std::string &copyName) {
		OBSSourceAutoRelease source = obs_get_source_by_name(copyName.c_str());
		obs_source_remove(source);
	};
	auto redo = [name](const std::string &originalUuid) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(originalUuid.c_str());
		OBSSceneAutoRelease duplicate =
			obs_scene_duplicate(obs_scene_from_source(source), name.c_str(), OBS_SCENE_DUP_REFS);
	};

	main->undo_s.add_action(QTStr("Undo.Scene.Duplicate").arg(name.c_str()), undo, redo, name,
				obs_source_get_uuid(sceneSource));
	return copySource;
}

void ExecContextMenu(QAbstractItemView *view, QMenu *menu, QContextMenuEvent *event)
{
	QPoint position = event->reason() == QContextMenuEvent::Keyboard
				  ? view->viewport()->mapToGlobal(view->visualRect(view->currentIndex()).center())
				  : event->globalPos();
	menu->exec(position);
}

} // namespace OBS::Accessibility
