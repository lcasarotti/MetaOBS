#include "AccessibleBridge.hpp"
#include "AccessibilityModule.hpp"

#include <OBSApp.hpp>
#include <utility/BasicOutputHandler.hpp>
#include <components/SourceTree.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSProjector.hpp>

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

QMenu *OBSAccessibleBridge::TransformMenu(OBSBasic *main)
{
	return main->ui->transformMenu;
}

QList<QAction *> OBSAccessibleBridge::SourceClipboardActions(OBSBasic *main)
{
	return {main->ui->actionCopySource, main->ui->actionPasteRef, main->ui->actionPasteDup};
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

bool OBSAccessibleBridge::MoveScene(OBSBasic *main, OBSScene scene, int row)
{
	QListWidget *list = main->ui->scenes;
	int from = -1;
	for (int i = 0; i < list->count(); i++) {
		if (GetOBSRef<OBSScene>(list->item(i)) == scene) {
			from = i;
			break;
		}
	}

	if (from < 0 || row < 0 || row >= list->count() || row == from) {
		return false;
	}

	/* As in ChangeSceneIndex, but the moved scene does not become the
	 * current one. */
	QListWidgetItem *current = list->currentItem();
	list->blockSignals(true);
	QListWidgetItem *item = list->takeItem(from);
	list->insertItem(row, item);
	list->setCurrentItem(current);
	list->blockSignals(false);

	OBSProjector::UpdateMultiviewProjectors();
	return true;
}

void OBSAccessibleBridge::GroupSelectedSources(OBSBasic *main)
{
	main->ui->sources->GroupSelectedItems();
}

void OBSAccessibleBridge::UngroupSelectedSources(OBSBasic *main)
{
	main->ui->sources->UngroupSelectedGroups();
}

void OBSAccessibleBridge::ResizeOutputToSelectedSource(OBSBasic *main)
{
	main->ResizeOutputSizeOfSource();
}

void OBSAccessibleBridge::CopyFilters(OBSBasic *main, obs_source_t *source)
{
	main->copyFiltersSource_ = obs_source_get_weak_source(source);
	main->ui->actionPasteFilters->setEnabled(true);
}

void OBSAccessibleBridge::PasteFilters(OBSBasic *main, obs_source_t *destination)
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(main->copyFiltersSource());
	main->SourcePasteFilters(source.Get(), destination);
}

bool OBSAccessibleBridge::HasCopiedFilters(OBSBasic *main)
{
	return !obs_weak_source_expired(main->copyFiltersSource());
}

void OBSAccessibleBridge::Screenshot(OBSBasic *main, obs_source_t *source)
{
	main->Screenshot(source);
}

void OBSAccessibleBridge::ScreenshotProgram(OBSBasic *main)
{
	main->ScreenshotProgram();
}
