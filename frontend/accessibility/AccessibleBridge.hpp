#pragma once

#include <obs.hpp>

#include <QList>
#include <QString>

#include <vector>

class OBSBasic;
class QAction;
class QDockWidget;
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

	/* The Transform submenu of the Edit menu, which acts on the selected
	 * scene items. */
	static QMenu *TransformMenu(OBSBasic *main);

	/* Copy, Paste (Reference) and Paste (Duplicate) of scene items, which
	 * act on the selected items and the current scene. */
	static QList<QAction *> SourceClipboardActions(OBSBasic *main);

	/* The main window's scene list, which holds the scene order. */
	static QListWidget *SceneList(OBSBasic *main);

	/* User-initiated output actions, with the same confirmations and checks
	 * as the main window's buttons. */
	static void StreamAction(OBSBasic *main);
	static void RecordAction(OBSBasic *main);
	static void RecordPauseToggle(OBSBasic *main);
	static void ReplayBufferAction(OBSBasic *main);
	static void VirtualCamAction(OBSBasic *main);
	static void OpenVirtualCamConfig(OBSBasic *main);
	static void ToggleStudioMode(OBSBasic *main);

	static bool IsRecordingPaused(OBSBasic *main);
	static bool IsReplayBufferAvailable(OBSBasic *main);
	static bool IsVirtualCamAvailable(OBSBasic *main);

	/* Shows or hides the main window and its dialogs, as the tray icon does. */
	static void SetMainWindowShowing(OBSBasic *main, bool showing);

	/* The docks added by plugins and the custom browser docks. */
	static QList<QDockWidget *> PluginDocks(OBSBasic *main);

	/* Opens the dialog that adds and removes custom browser docks, when
	 * OBS is built with browser support, and returns whether it can. */
	static bool ManageBrowserDocks(OBSBasic *main);
	static bool HasBrowserDocks();

	/* The tray icon's menu, or nullptr when there is no tray. */
	static QMenu *TrayMenu(OBSBasic *main);

	/* Sets the tray menu's Show/Hide item for a window that is or is not on
	 * screen. */
	static void UpdateTrayShowHide(OBSBasic *main, bool showing);

	/* Whether minimizing should hide OBS in the tray, as set in Settings. */
	static bool MinimizesToTray(OBSBasic *main);

	static void AddScene(OBSBasic *main);
	static void RemoveScene(OBSBasic *main, OBSScene scene);

	/* Moves a scene to another row of the scene list, which holds the
	 * scene order, without changing the current scene. */
	static bool MoveScene(OBSBasic *main, OBSScene scene, int row);

	/* Act on the selected scene items of the current scene. */
	static void RemoveSelectedSources(OBSBasic *main);
	static void MoveSelectedSources(OBSBasic *main, obs_order_movement movement);
	static void GroupSelectedSources(OBSBasic *main);
	static void UngroupSelectedSources(OBSBasic *main);
	static void ResizeOutputToSelectedSource(OBSBasic *main);

	/* The filters clipboard, shared with the main window. */
	static void CopyFilters(OBSBasic *main, obs_source_t *source);
	static void PasteFilters(OBSBasic *main, obs_source_t *destination);
	static bool HasCopiedFilters(OBSBasic *main);

	/* Saves a screenshot of the source, or of the program output. */
	static void Screenshot(OBSBasic *main, obs_source_t *source);
	static void ScreenshotProgram(OBSBasic *main);

	/* The transitions in the order of the main window's list. */
	static std::vector<OBSSource> Transitions(OBSBasic *main);

	/* Asks for a name and adds a transition of the given type, as the main
	 * window's + button does. */
	static void AddTransition(OBSBasic *main, const char *id);
	static void RenameCurrentTransition(OBSBasic *main);
	static void RemoveCurrentTransition(OBSBasic *main);

	/* The quick transitions of Studio Mode, with the text of their
	 * buttons. */
	struct QuickTransitionInfo {
		int id;
		QString text;
	};
	static std::vector<QuickTransitionInfo> QuickTransitions(OBSBasic *main);
	static void TriggerQuickTransition(OBSBasic *main, int id);

	/* The T-bar of Studio Mode, from 0 to TBarMaximum(). Moving it starts
	 * a manual transition, and releasing it near either end finishes or
	 * cancels the transition. Does nothing outside Studio Mode. */
	static int TBarMaximum();
	static void SetTBarPosition(OBSBasic *main, int position);
	static void ReleaseTBar(OBSBasic *main);
};
