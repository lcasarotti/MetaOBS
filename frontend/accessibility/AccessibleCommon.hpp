#pragma once

#include <obs.hpp>

#include <QKeySequence>
#include <QList>
#include <QString>

#include <vector>

class OBSBasic;
class QAbstractItemView;
class QAction;
class QContextMenuEvent;
class QMenu;
class QTreeView;
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

/* Selects the items and deselects every other item of the scene, including
 * items inside groups, so the main window's actions act on them. */
void SelectOnly(obs_scene_t *scene, const std::vector<obs_sceneitem_t *> &items);

void Announce(const QString &text);

/* Speaks the answer to something the user asked for, such as a status
 * readout, whatever the announcement level. */
void AnnounceRequested(const QString &text);

/* Creates a panel action. Its shortcuts work while the owner has focus, so
 * panels can use the same keys for different actions. */
QAction *CreatePanelAction(QWidget *owner, const QString &text, const QList<QKeySequence> &shortcuts = {});

/* An action of the accessible window that follows the text, shortcuts and
 * state of a main window action, and triggers it. Sharing the main window's
 * action instead would tie its accessible interface to the main window's
 * menus, where screen readers cannot reach it. Its shortcuts are only active
 * while the accessible window is, and the main window's while it is.
 * Shortcuts in reserved are left out, because the panels use them, and two
 * shortcuts with the same keys would cancel each other. */
QAction *CreateMirrorAction(QAction *target, QObject *parent, const QList<QKeySequence> &reserved = {});

/* Fills a menu with one item per monitor and a "New window" item, each
 * opening a projector of the given type (see obs_frontend_open_projector). */
void FillProjectorMenu(QMenu *menu, const char *type, const QString &sourceName = {});

/* Hides the source in the audio mixer, or shows it again. */
void SetHiddenInMixer(OBSBasic *main, obs_source_t *source, bool hidden);
bool IsHiddenInMixer(obs_source_t *source);

/* Asks for a name and duplicates the scene, with its sources as references,
 * like the main window's Duplicate, without switching to the copy. Returns
 * the new scene, or nullptr if the user cancelled. */
OBSSource DuplicateSceneWithUndo(QWidget *parent, OBSBasic *main, obs_source_t *sceneSource);

/* Qt does not tell screen readers when a tree branch is collapsed, so the
 * rows below it keep describing the hidden items, and after a branch is
 * expanded it rebuilds the tree without reporting the focused item again.
 * Either way NVDA may not read the branches. This rebuilds the tree's
 * accessible rows and reports the current item after each change. */
void KeepTreeExpansionAccessible(QTreeView *view);

/* Opens a panel's menu as the context menu of a list, next to the current
 * item when it was opened from the keyboard. */
void ExecContextMenu(QAbstractItemView *view, QMenu *menu, QContextMenuEvent *event);

} // namespace OBS::Accessibility
