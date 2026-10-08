#include "AudioPropertiesDialog.hpp"
#include "AccessibleFormat.hpp"
#include "AudioActions.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <string>

#include "moc_AudioPropertiesDialog.cpp"

namespace OBS::Accessibility {

namespace {
/* The same ranges as the Advanced Audio Properties dialog. */
constexpr double volumeMinimumDb = -100.0;
constexpr double volumeMaximumDb = 26.0;
constexpr int syncOffsetMinimumMs = -950;
constexpr int syncOffsetMaximumMs = 20000;
constexpr int64_t nsPerMs = 1000000;

constexpr int trackCount = 6;

int BalanceToPercent(float balance)
{
	return static_cast<int>(std::lround((balance - 0.5f) * 200.0f));
}

float PercentToBalance(int percent)
{
	return static_cast<float>(percent) / 200.0f + 0.5f;
}
} // namespace

void AudioPropertiesDialog::Open(OBSBasic *main, obs_source_t *source, QWidget *parent)
{
	AudioPropertiesDialog dialog(main, source, parent);
	dialog.exec();
}

AudioPropertiesDialog::AudioPropertiesDialog(OBSBasic *main_, obs_source_t *source, QWidget *parent)
	: QDialog(parent),
	  main(main_),
	  weakSource(OBSGetWeakRef(source))
{
	QString name = QT_UTF8(obs_source_get_name(source));
	setWindowTitle(QTStr("Accessibility.AudioProperties.Title").arg(name));

	QVBoxLayout *layout = new QVBoxLayout(this);
	QFormLayout *form = new QFormLayout();
	layout->addLayout(form);

	QDoubleSpinBox *volume = new QDoubleSpinBox(this);
	volume->setRange(volumeMinimumDb, volumeMaximumDb);
	volume->setDecimals(1);
	volume->setSingleStep(1.0);
	volume->setSuffix(" dB");
	volume->setSpecialValueText("-inf dB");
	float volumeDb = GetVolumeDb(source);
	volume->setValue(volumeDb <= minimumVolumeDb ? volumeMinimumDb : volumeDb);
	form->addRow(QTStr("Basic.AdvAudio.Volume"), volume);

	QCheckBox *muted = new QCheckBox(QTStr("Mute"), this);
	muted->setChecked(obs_source_muted(source));
	form->addRow(muted);

	QCheckBox *monitoring = new QCheckBox(QTStr("Basic.AdvAudio.Monitoring"), this);
	monitoring->setChecked(obs_source_get_monitoring_enabled(source));
	form->addRow(monitoring);

	QSpinBox *balance = new QSpinBox(this);
	balance->setRange(-100, 100);
	balance->setSingleStep(5);
	balance->setValue(BalanceToPercent(obs_source_get_balance_value(source)));
	balance->setAccessibleDescription(QTStr("Accessibility.AudioProperties.Balance.Description"));
	form->addRow(QTStr("Basic.AdvAudio.Balance"), balance);

	QSpinBox *syncOffset = new QSpinBox(this);
	syncOffset->setRange(syncOffsetMinimumMs, syncOffsetMaximumMs);
	syncOffset->setSingleStep(10);
	syncOffset->setSuffix(" ms");
	syncOffset->setValue(static_cast<int>(obs_source_get_sync_offset(source) / nsPerMs));
	form->addRow(QTStr("Basic.AdvAudio.SyncOffset"), syncOffset);

	QCheckBox *mono = new QCheckBox(QTStr("Basic.AdvAudio.Mono"), this);
	mono->setChecked(IsMono(source));
	form->addRow(mono);

	QGroupBox *tracks = new QGroupBox(QTStr("Basic.AdvAudio.AudioTracks"), this);
	QVBoxLayout *tracksLayout = new QVBoxLayout(tracks);
	uint32_t mixers = obs_source_get_audio_mixers(source);
	std::vector<QCheckBox *> trackBoxes;
	for (int track = 0; track < trackCount; track++) {
		std::string key = "Basic.Settings.Output.Adv.Audio.Track" + std::to_string(track + 1);
		QCheckBox *trackBox = new QCheckBox(QTStr(key.c_str()), tracks);
		trackBox->setChecked((mixers & (1u << track)) != 0);
		tracksLayout->addWidget(trackBox);
		trackBoxes.push_back(trackBox);
	}
	layout->addWidget(tracks);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	/* Each change applies to the source at once, if it still exists. */
	auto withSource = [this](auto apply) {
		return [this, apply](auto value) {
			if (OBSSource target = OBSGetStrongRef(weakSource)) {
				apply(target.Get(), value);
			}
		};
	};

	connect(volume, &QDoubleSpinBox::valueChanged, this, withSource([this](obs_source_t *target, double value) {
			SetVolumeDbWithUndo(main, target, value <= volumeMinimumDb ? minimumVolumeDb : value);
		}));
	connect(muted, &QCheckBox::toggled, this, withSource([this](obs_source_t *target, bool value) {
			SetMutedWithUndo(main, target, value);
		}));
	connect(monitoring, &QCheckBox::toggled, this, withSource([this](obs_source_t *target, bool value) {
			SetMonitoringWithUndo(main, target, value);
		}));
	connect(balance, &QSpinBox::valueChanged, this, withSource([this](obs_source_t *target, int value) {
			SetBalanceWithUndo(main, target, PercentToBalance(value));
		}));
	connect(syncOffset, &QSpinBox::valueChanged, this, withSource([this](obs_source_t *target, int value) {
			SetSyncOffsetWithUndo(main, target, value * nsPerMs);
		}));
	connect(mono, &QCheckBox::toggled, this, withSource([this](obs_source_t *target, bool value) {
			SetMonoWithUndo(main, target, value);
		}));

	for (int track = 0; track < trackCount; track++) {
		connect(trackBoxes[track], &QCheckBox::toggled, this,
			withSource([this, track](obs_source_t *target, bool value) {
				uint32_t current = obs_source_get_audio_mixers(target);
				uint32_t bit = 1u << track;
				SetMixersWithUndo(main, target, value ? (current | bit) : (current & ~bit));
			}));
	}
}

} // namespace OBS::Accessibility
