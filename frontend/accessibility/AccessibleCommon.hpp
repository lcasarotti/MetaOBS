#pragma once

#include <obs.hpp>

#include <QKeySequence>
#include <QList>
#include <QString>

class OBSBasic;
class QAbstractItemView;
class QAction;
class QContextMenuEvent;
class QMenu;
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

/* Speaks the answer to something the user asked for, such as a status
 * readout, whatever the announcement level. */
void AnnounceRequested(const QString &text);

/* Creates a panel action. Its shortcuts work while the owner has focus, so
 * panels can use the same keys for different actions. */
QAction *CreatePanelAction(QWidget *owner, const QString &text, const QList<QKeySequence> &shortcuts = {});

/* Opens a panel's menu as the context menu of a list, next to the current
 * item when it was opened from the keyboard. */
void ExecContextMenu(QAbstractItemView *view, QMenu *menu, QContextMenuEvent *event);

} // namespace OBS::Accessibility
