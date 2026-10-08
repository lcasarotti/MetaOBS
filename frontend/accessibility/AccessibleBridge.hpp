#pragma once

#include <obs.hpp>

class OBSBasic;
class QAction;
class QListWidget;
class QMenu;

/* The only class of the accessibility module that reaches into OBSBasic's
 * private members. Keeping these accesses in one place means an upstream
 * refactor breaks this file at compile time instead of silently breaking the
 * accessible interface. */
class OBSAccessibleBridge {
public:
	/* Menus of the main window reused by the accessible window. */
	static QMenu *FileMenu(OBSBasic *main);
	static QMenu *ProfileMenu(OBSBasic *main);
	static QMenu *SceneCollectionMenu(OBSBasic *main);
	static QMenu *ToolsMenu(OBSBasic *main);
	static QMenu *HelpMenu(OBSBasic *main);
	static QMenu *ViewMenu(OBSBasic *main);

	static QAction *UndoAction(OBSBasic *main);
	static QAction *RedoAction(OBSBasic *main);
	static QAction *AddSourceAction(OBSBasic *main);
	static QAction *EditTransformAction(OBSBasic *main);

	/* The main window's scene list, which holds the scene order. */
	static QListWidget *SceneList(OBSBasic *main);

	/* User-initiated output actions, with the same confirmations and checks
	 * as the main window's buttons. */
	static void StreamAction(OBSBasic *main);
	static void RecordAction(OBSBasic *main);
	static void RecordPauseToggle(OBSBasic *main);
	static void ReplayBufferAction(OBSBasic *main);
	static void VirtualCamAction(OBSBasic *main);
	static void ToggleStudioMode(OBSBasic *main);

	static bool IsRecordingPaused(OBSBasic *main);
	static bool IsReplayBufferAvailable(OBSBasic *main);
	static bool IsVirtualCamAvailable(OBSBasic *main);

	/* Shows or hides the main window and its dialogs, as the tray icon does. */
	static void SetMainWindowShowing(OBSBasic *main, bool showing);

	static void AddScene(OBSBasic *main);
	static void RemoveScene(OBSBasic *main, OBSScene scene);

	/* Act on the selected scene items of the current scene. */
	static void RemoveSelectedSources(OBSBasic *main);
	static void MoveSelectedSources(OBSBasic *main, obs_order_movement movement);
};
