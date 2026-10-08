#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QGroupBox>

class OBSBasic;
class QAction;
class QListView;
class QMenu;
class QStandardItemModel;
class QTimer;

namespace OBS::Accessibility {

/* List of scenes in the main window's order. Moving through the list never
 * changes the scene: Enter does, so browsing during a stream is safe. */
class ScenesPanel : public QGroupBox {
	Q_OBJECT

public:
	ScenesPanel(OBSBasic *main, QWidget *parent = nullptr);
	~ScenesPanel();

	QListView *View() const { return view; }
	QMenu *Menu() const { return menu; }

private:
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	void HandleFrontendEvent(enum obs_frontend_event event);

	void ScheduleRefresh();
	void Refresh();
	void CreateMenu();

	OBSSource GetSelectedScene() const;
	void SwitchToSelectedScene();
	void RenameSelectedScene();
	void RemoveSelectedScene();
	void OpenSelectedSceneFilters();

	OBSBasic *main;
	QListView *view;
	QStandardItemModel *model;
	QTimer *refreshTimer;
	QMenu *menu;
	bool collectionLoading = false;

	QAction *switchAction;
	QAction *addAction;
	QAction *renameAction;
	QAction *removeAction;
	QAction *filtersAction;
};

} // namespace OBS::Accessibility
