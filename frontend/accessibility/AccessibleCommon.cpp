#include "AccessibleCommon.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>
#include <dialogs/NameDialog.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <functional>
#include <string>

namespace OBS::Accessibility {

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

void SelectExclusively(obs_scene_t *scene, obs_sceneitem_t *item)
{
	struct SelectData {
		obs_sceneitem_t *target;
	} data = {item};

	auto select = [](obs_scene_t *, obs_sceneitem_t *sceneItem, void *param) {
		auto selectData = static_cast<SelectData *>(param);
		bool selected = sceneItem == selectData->target;
		if (obs_sceneitem_selected(sceneItem) != selected) {
			obs_sceneitem_select(sceneItem, selected);
		}
		/* Recurse into groups, whose items can be selected too. */
		if (obs_sceneitem_is_group(sceneItem)) {
			obs_sceneitem_group_enum_items(
				sceneItem,
				[](obs_scene_t *, obs_sceneitem_t *child, void *childParam) {
					auto childData = static_cast<SelectData *>(childParam);
					bool childSelected = child == childData->target;
					if (obs_sceneitem_selected(child) != childSelected) {
						obs_sceneitem_select(child, childSelected);
					}
					return true;
				},
				param);
		}
		return true;
	};

	obs_scene_enum_items(scene, select, &data);
}

void Announce(const QString &text)
{
	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(text, Priority::High);
	}
}

} // namespace OBS::Accessibility
