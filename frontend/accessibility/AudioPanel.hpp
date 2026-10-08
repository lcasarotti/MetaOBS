#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QGroupBox>

#include <mutex>
#include <vector>

class OBSBasic;
class QAction;
class QListView;
class QMenu;
class QStandardItemModel;
class QTimer;

namespace OBS::Accessibility {

/* List of the sources shown in the audio mixer, with each source's volume and
 * state in its text. The keys change the volume, mute, monitoring, and read
 * the audio levels of the current source on request. */
class AudioPanel : public QGroupBox {
	Q_OBJECT

public:
	AudioPanel(OBSBasic *main, QWidget *parent = nullptr);
	~AudioPanel();

	QListView *View() const { return view; }
	QMenu *Menu() const { return menu; }

private:
	struct Entry {
		QString uuid;
		QString text;
	};

	struct Levels {
		float peak;
		float magnitude;
		float heldPeak;
		uint64_t heldPeakTime;
	};

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnSourceSignal(void *data, calldata_t *params);
	static void OnLevels(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
			     const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	void CreateMenu();
	void ConnectGlobalSignals();
	void DisconnectAll();
	void ScheduleRefresh();
	void Refresh();
	std::vector<Entry> CollectEntries() const;
	void ConnectSourceSignals(const std::vector<Entry> &entries);

	OBSSource GetSelectedSource() const;
	void AttachMeter();
	void ResetLevels();

	void ChangeVolume(float step);
	void ToggleMute();
	void ToggleMonitoring();
	void ReadLevels();
	void OpenAudioProperties();
	void OpenFilters();
	void OpenProperties();

	OBSBasic *main;
	QListView *view;
	QStandardItemModel *model;
	QTimer *refreshTimer;
	QMenu *menu;

	std::vector<OBSSignal> globalSignals;
	std::vector<OBSSignal> sourceSignals;
	QStringList listedUuids;

	obs_volmeter_t *volmeter = nullptr;
	OBSWeakSource meterSource;
	std::mutex levelsMutex;
	Levels levels;

	QAction *volumeUpAction;
	QAction *volumeDownAction;
	QAction *volumeUpLargeAction;
	QAction *volumeDownLargeAction;
	QAction *muteAction;
	QAction *monitoringAction;
	QAction *readLevelsAction;
	QAction *audioPropertiesAction;
	QAction *filtersAction;
	QAction *propertiesAction;
	QAction *advancedAudioAction;
};

} // namespace OBS::Accessibility
