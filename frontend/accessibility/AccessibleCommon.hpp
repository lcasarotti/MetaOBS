#pragma once

#include <obs.hpp>

#include <QString>

class OBSBasic;
class QWidget;

namespace OBS::Accessibility {

/* Asks for a new name and renames the source, with undo. The main window
 * renames in place in its lists, which the accessible window does not use. */
void RenameSourceWithUndo(QWidget *parent, OBSBasic *main, obs_source_t *source);

/* Shows or hides a scene item, with undo, like the main window's visibility
 * checkbox. */
void SetItemVisibleWithUndo(OBSBasic *main, obs_sceneitem_t *item, bool visible);

/* Finds a scene item by its id in the scene or group with the given UUID. */
OBSSceneItem FindSceneItem(const QString &sceneUuid, int64_t itemId);

/* Selects the item and deselects every other item of the scene, including
 * items inside groups, so the main window's actions act on it. */
void SelectExclusively(obs_scene_t *scene, obs_sceneitem_t *item);

void Announce(const QString &text);

} // namespace OBS::Accessibility
