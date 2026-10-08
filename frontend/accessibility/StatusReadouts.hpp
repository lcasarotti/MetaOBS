#pragma once

#include <obs-frontend-api.h>
#include <obs.h>

#include <QObject>
#include <QString>

#include <vector>

class OBSBasic;

namespace OBS::Accessibility {

/* Something the user can ask OBS to read aloud: the status summary, the
 * stream health and so on. Each readout is available as an OBS hotkey, which
 * also works while OBS is in the background, and in the accessible window's
 * Status menu. */
struct Readout {
	const char *hotkeyName;
	const char *lookupKey;
	QString (*text)();
};

const std::vector<Readout> &GetReadouts();

/* Registers one OBS hotkey per readout, plus one that switches between the
 * accessible and the standard interface. None has a default key; they are
 * assigned in Settings > Hotkeys and saved with the profile, like the main
 * window's hotkeys. */
class StatusHotkeys : public QObject {
	Q_OBJECT

public:
	explicit StatusHotkeys(OBSBasic *main);
	~StatusHotkeys();

private:
	struct Registration {
		obs_hotkey_id id;
		const char *name;
	};

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	void Register(const char *name, const char *lookupKey);
	void LoadBindings();
	void UnregisterAll();

	OBSBasic *main;
	std::vector<Registration> registrations;
};

} // namespace OBS::Accessibility
