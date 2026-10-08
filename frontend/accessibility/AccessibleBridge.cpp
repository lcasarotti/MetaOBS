#include "AccessibleBridge.hpp"
#include "AccessibilityModule.hpp"

#include <OBSApp.hpp>
#include <utility/BasicOutputHandler.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

bool OBSBasic::IsUserFacing() const
{
	return isVisible() || OBS::Accessibility::IsAccessibleModeActive();
}

QMenu *OBSAccessibleBridge::FileMenu(OBSBasic *main)
{
	return main->ui->menu_File;
}

QMenu *OBSAccessibleBridge::ProfileMenu(OBSBasic *main)
{
	return main->ui->profileMenu;
}

QMenu *OBSAccessibleBridge::SceneCollectionMenu(OBSBasic *main)
{
	return main->ui->sceneCollectionMenu;
}

QMenu *OBSAccessibleBridge::ToolsMenu(OBSBasic *main)
{
	return main->ui->menuTools;
}

QMenu *OBSAccessibleBridge::HelpMenu(OBSBasic *main)
{
	return main->ui->menuBasic_MainMenu_Help;
}

QMenu *OBSAccessibleBridge::ViewMenu(OBSBasic *main)
{
	return main->ui->viewMenu;
}

QAction *OBSAccessibleBridge::UndoAction(OBSBasic *main)
{
	return main->ui->actionMainUndo;
}

QAction *OBSAccessibleBridge::RedoAction(OBSBasic *main)
{
	return main->ui->actionMainRedo;
}

QAction *OBSAccessibleBridge::AddSourceAction(OBSBasic *main)
{
	return main->ui->actionAddSource;
}

QAction *OBSAccessibleBridge::EditTransformAction(OBSBasic *main)
{
	return main->ui->actionEditTransform;
}

QListWidget *OBSAccessibleBridge::SceneList(OBSBasic *main)
{
	return main->ui->scenes;
}

void OBSAccessibleBridge::StreamAction(OBSBasic *main)
{
	main->StreamActionTriggered();
}

void OBSAccessibleBridge::RecordAction(OBSBasic *main)
{
	main->RecordActionTriggered();
}

void OBSAccessibleBridge::RecordPauseToggle(OBSBasic *main)
{
	main->RecordPauseToggled();
}

void OBSAccessibleBridge::ReplayBufferAction(OBSBasic *main)
{
	main->ReplayBufferActionTriggered();
}

void OBSAccessibleBridge::VirtualCamAction(OBSBasic *main)
{
	main->VirtualCamActionTriggered();
}

void OBSAccessibleBridge::ToggleStudioMode(OBSBasic *main)
{
	main->TogglePreviewProgramMode();
}

bool OBSAccessibleBridge::IsRecordingPaused(OBSBasic *)
{
	return obs_frontend_recording_paused();
}

bool OBSAccessibleBridge::IsReplayBufferAvailable(OBSBasic *main)
{
	return main->outputHandler && main->outputHandler->replayBuffer;
}

bool OBSAccessibleBridge::IsVirtualCamAvailable(OBSBasic *main)
{
	return main->vcamEnabled;
}

void OBSAccessibleBridge::SetMainWindowShowing(OBSBasic *main, bool showing)
{
	main->SetShowing(showing);
}

void OBSAccessibleBridge::AddScene(OBSBasic *main)
{
	main->on_actionAddScene_triggered();
}

void OBSAccessibleBridge::RemoveScene(OBSBasic *main, OBSScene scene)
{
	QListWidget *list = main->ui->scenes;
	for (int row = 0; row < list->count(); row++) {
		if (GetOBSRef<OBSScene>(list->item(row)) == scene) {
			main->RemoveSceneWithUndo(scene, row);
			return;
		}
	}
}

void OBSAccessibleBridge::RemoveSelectedSources(OBSBasic *main)
{
	main->on_actionRemoveSource_triggered();
}

void OBSAccessibleBridge::MoveSelectedSources(OBSBasic *main, obs_order_movement movement)
{
	switch (movement) {
	case OBS_ORDER_MOVE_UP:
		main->on_actionMoveUp_triggered();
		break;
	case OBS_ORDER_MOVE_DOWN:
		main->on_actionMoveDown_triggered();
		break;
	case OBS_ORDER_MOVE_TOP:
		main->on_actionMoveToTop_triggered();
		break;
	case OBS_ORDER_MOVE_BOTTOM:
		main->on_actionMoveToBottom_triggered();
		break;
	}
}
