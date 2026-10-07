#include "AccessibleSettings.hpp"

#include <OBSApp.hpp>

#include <util/config-file.h>

#include <algorithm>

namespace OBS::Accessibility::Settings {

namespace {
constexpr const char *section = "Accessibility";

constexpr const char *announceLevelKey = "AnnounceLevel";
constexpr const char *announceOutputsKey = "AnnounceOutputs";
constexpr const char *announceStatusMessagesKey = "AnnounceStatusMessages";
constexpr const char *speechBackendKey = "SpeechBackend";

config_t *GetConfig()
{
	return App()->GetUserConfig();
}
} // namespace

void SetDefaults()
{
	config_t *config = GetConfig();

	config_set_default_int(config, section, announceLevelKey, static_cast<int>(AnnounceLevel::Normal));
	config_set_default_bool(config, section, announceOutputsKey, true);
	config_set_default_bool(config, section, announceStatusMessagesKey, true);
	config_set_default_int(config, section, speechBackendKey, static_cast<int>(SpeechBackend::Automatic));
}

AnnounceLevel GetAnnounceLevel()
{
	long long value = config_get_int(GetConfig(), section, announceLevelKey);
	value = std::clamp<long long>(value, static_cast<int>(AnnounceLevel::Off),
				      static_cast<int>(AnnounceLevel::Verbose));
	return static_cast<AnnounceLevel>(value);
}

void SetAnnounceLevel(AnnounceLevel level)
{
	config_set_int(GetConfig(), section, announceLevelKey, static_cast<int>(level));
}

bool GetAnnounceOutputs()
{
	return config_get_bool(GetConfig(), section, announceOutputsKey);
}

void SetAnnounceOutputs(bool enabled)
{
	config_set_bool(GetConfig(), section, announceOutputsKey, enabled);
}

bool GetAnnounceStatusMessages()
{
	return config_get_bool(GetConfig(), section, announceStatusMessagesKey);
}

void SetAnnounceStatusMessages(bool enabled)
{
	config_set_bool(GetConfig(), section, announceStatusMessagesKey, enabled);
}

SpeechBackend GetSpeechBackend()
{
	long long value = config_get_int(GetConfig(), section, speechBackendKey);
	value = std::clamp<long long>(value, static_cast<int>(SpeechBackend::Automatic),
				      static_cast<int>(SpeechBackend::NvdaController));
	return static_cast<SpeechBackend>(value);
}

void SetSpeechBackend(SpeechBackend backend)
{
	config_set_int(GetConfig(), section, speechBackendKey, static_cast<int>(backend));
}

} // namespace OBS::Accessibility::Settings
