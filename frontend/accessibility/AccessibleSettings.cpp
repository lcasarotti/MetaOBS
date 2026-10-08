#include "AccessibleSettings.hpp"

#include <OBSApp.hpp>

#include <util/config-file.h>

#include <algorithm>

namespace OBS::Accessibility::Settings {

namespace {
constexpr const char *section = "Accessibility";

constexpr const char *accessibleModeKey = "AccessibleMode";
constexpr const char *announceLevelKey = "AnnounceLevel";
constexpr const char *announceOutputsKey = "AnnounceOutputs";
constexpr const char *announceStatusMessagesKey = "AnnounceStatusMessages";
constexpr const char *speechBackendKey = "SpeechBackend";
constexpr const char *announceWarningsKey = "AnnounceWarnings";
constexpr const char *frameLossThresholdKey = "FrameLossThreshold";

config_t *GetConfig()
{
	return App()->GetUserConfig();
}
} // namespace

void SetDefaults()
{
	config_t *config = GetConfig();

	config_set_default_bool(config, section, accessibleModeKey, false);
	config_set_default_int(config, section, announceLevelKey, static_cast<int>(AnnounceLevel::Normal));
	config_set_default_bool(config, section, announceOutputsKey, true);
	config_set_default_bool(config, section, announceStatusMessagesKey, true);
	config_set_default_int(config, section, speechBackendKey, static_cast<int>(SpeechBackend::Automatic));
	config_set_default_bool(config, section, announceWarningsKey, true);
	config_set_default_int(config, section, frameLossThresholdKey, defaultFrameLossThreshold);
}

bool GetAccessibleMode()
{
	return config_get_bool(GetConfig(), section, accessibleModeKey);
}

void SetAccessibleMode(bool enabled)
{
	config_set_bool(GetConfig(), section, accessibleModeKey, enabled);
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

bool GetAnnounceWarnings()
{
	return config_get_bool(GetConfig(), section, announceWarningsKey);
}

void SetAnnounceWarnings(bool enabled)
{
	config_set_bool(GetConfig(), section, announceWarningsKey, enabled);
}

int GetFrameLossThreshold()
{
	long long value = config_get_int(GetConfig(), section, frameLossThresholdKey);
	return static_cast<int>(std::clamp<long long>(value, minimumFrameLossThreshold, maximumFrameLossThreshold));
}

void SetFrameLossThreshold(int percent)
{
	config_set_int(GetConfig(), section, frameLossThresholdKey, percent);
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
