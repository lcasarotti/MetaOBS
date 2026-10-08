#include "StatusReadouts.hpp"
#include "AccessibilityModule.hpp"
#include "AccessibleCommon.hpp"
#include "StatusMonitor.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <cstring>

#include "moc_StatusReadouts.cpp"

namespace OBS::Accessibility {

namespace {
constexpr const char *toggleModeHotkeyName = "Accessibility.ToggleMode";

template<QString (StatusMonitor::*... Parts)() const> QString Join()
{
	StatusMonitor *monitor = StatusMonitor::Get();
	if (!monitor) {
		return {};
	}

	QStringList parts = {(monitor->*Parts)()...};
	return parts.join(". ");
}

QString SceneText()
{
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	QString text = QTStr("Accessibility.Announce.SceneLive").arg(QT_UTF8(obs_source_get_name(program)));

	if (obs_frontend_preview_program_mode_active()) {
		OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
		text += ". " + QTStr("Accessibility.Announce.ScenePreview").arg(QT_UTF8(obs_source_get_name(preview)));
	}

	return text;
}
} // namespace

const std::vector<Readout> &GetReadouts()
{
	static const std::vector<Readout> readouts = {
		{"Accessibility.ReadSummary", "Accessibility.Readout.Summary", Join<&StatusMonitor::SummaryText>},
		{"Accessibility.ReadStreaming", "Accessibility.Readout.Streaming",
		 Join<&StatusMonitor::StreamingText, &StatusMonitor::StreamHealthText>},
		{"Accessibility.ReadRecording", "Accessibility.Readout.Recording", Join<&StatusMonitor::RecordingText>},
		{"Accessibility.ReadPerformance", "Accessibility.Readout.Performance",
		 Join<&StatusMonitor::PerformanceText, &StatusMonitor::LagText>},
		{"Accessibility.ReadDiskSpace", "Accessibility.Readout.DiskSpace", Join<&StatusMonitor::DiskSpaceText>},
		{"Accessibility.ReadScene", "Accessibility.Readout.Scene", SceneText},
		{"Accessibility.ReadLastMessage", "Accessibility.Readout.LastMessage",
		 Join<&StatusMonitor::LastMessageText>},
	};
	return readouts;
}

StatusHotkeys::StatusHotkeys(OBSBasic *main_) : QObject(main_), main(main_)
{
	for (const Readout &readout : GetReadouts()) {
		Register(readout.hotkeyName, readout.lookupKey);
	}
	Register(toggleModeHotkeyName, "Accessibility.Hotkey.ToggleMode");

	LoadBindings();
	obs_frontend_add_event_callback(OnFrontendEvent, this);
}

StatusHotkeys::~StatusHotkeys()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	UnregisterAll();
}

void StatusHotkeys::Register(const char *name, const char *lookupKey)
{
	/* The readout texts are menu items, with mnemonics. */
	QString label = QTStr(lookupKey).remove('&');
	QString description = QTStr("Accessibility.Hotkey.Description").arg(label);
	obs_hotkey_id id = obs_hotkey_register_frontend(name, QT_TO_UTF8(description), OnHotkey, this);
	registrations.push_back({id, name});
}

/* Settings > Hotkeys saves frontend hotkeys in the profile's [Hotkeys]
 * section, under their names. */
void StatusHotkeys::LoadBindings()
{
	for (const Registration &registration : registrations) {
		const char *json = config_get_string(main->Config(), "Hotkeys", registration.name);
		OBSDataAutoRelease data = json ? obs_data_create_from_json(json) : nullptr;
		OBSDataArrayAutoRelease bindings = data ? obs_data_get_array(data, "bindings") : nullptr;
		obs_hotkey_load(registration.id, bindings);
	}
}

void StatusHotkeys::UnregisterAll()
{
	for (const Registration &registration : registrations) {
		obs_hotkey_unregister(registration.id);
	}
	registrations.clear();
}

void StatusHotkeys::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto hotkeys = static_cast<StatusHotkeys *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		hotkeys->LoadBindings();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		hotkeys->UnregisterAll();
		break;
	default:
		break;
	}
}

/* The main window routes hotkeys to the UI thread. */
void StatusHotkeys::OnHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	auto hotkeys = static_cast<StatusHotkeys *>(data);
	if (!pressed) {
		return;
	}

	for (const Registration &registration : hotkeys->registrations) {
		if (registration.id != id) {
			continue;
		}

		if (strcmp(registration.name, toggleModeHotkeyName) == 0) {
			if (IsAccessibleModeActive()) {
				SwitchToStandardMode();
			} else {
				SwitchToAccessibleMode();
			}
			return;
		}

		for (const Readout &readout : GetReadouts()) {
			if (strcmp(registration.name, readout.hotkeyName) == 0) {
				AnnounceRequested(readout.text());
				return;
			}
		}
	}
}

} // namespace OBS::Accessibility
