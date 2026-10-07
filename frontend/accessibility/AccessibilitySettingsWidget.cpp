#include "AccessibilitySettingsWidget.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>

#include "moc_AccessibilitySettingsWidget.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int delayedTestMs = 5000;

QComboBox *CreateComboBox(QWidget *parent, std::initializer_list<const char *> lookupKeys)
{
	QComboBox *comboBox = new QComboBox(parent);
	for (const char *key : lookupKeys) {
		comboBox->addItem(QTStr(key));
	}
	return comboBox;
}
} // namespace

AccessibilitySettingsWidget::AccessibilitySettingsWidget(QWidget *parent)
	: QGroupBox(QTStr("Accessibility.Settings.ScreenReader"), parent)
{
	QFormLayout *layout = new QFormLayout(this);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	announceLevel = CreateComboBox(this, {"Accessibility.Settings.AnnounceLevel.Off",
					      "Accessibility.Settings.AnnounceLevel.Essential",
					      "Accessibility.Settings.AnnounceLevel.Normal",
					      "Accessibility.Settings.AnnounceLevel.Verbose"});
	announceLevel->setAccessibleName(QTStr("Accessibility.Settings.AnnounceLevel"));
	announceLevel->setAccessibleDescription(QTStr("Accessibility.Settings.AnnounceLevel.Description"));
	layout->addRow(QTStr("Accessibility.Settings.AnnounceLevel"), announceLevel);

	announceOutputs = new QCheckBox(QTStr("Accessibility.Settings.AnnounceOutputs"), this);
	layout->addRow(announceOutputs);

	announceStatusMessages = new QCheckBox(QTStr("Accessibility.Settings.AnnounceStatusMessages"), this);
	layout->addRow(announceStatusMessages);

	speechBackend = CreateComboBox(this, {"Accessibility.Settings.SpeechBackend.Automatic",
					      "Accessibility.Settings.SpeechBackend.UIAutomation",
					      "Accessibility.Settings.SpeechBackend.NvdaController"});
	speechBackend->setAccessibleName(QTStr("Accessibility.Settings.SpeechBackend"));
	layout->addRow(QTStr("Accessibility.Settings.SpeechBackend"), speechBackend);

	testNow = new QPushButton(QTStr("Accessibility.Settings.TestAnnouncement"), this);
	testDelayed = new QPushButton(QTStr("Accessibility.Settings.TestAnnouncementDelayed"), this);
	testDelayed->setAccessibleDescription(QTStr("Accessibility.Settings.TestAnnouncementDelayed.Description"));

	QHBoxLayout *testButtons = new QHBoxLayout();
	testButtons->addWidget(testNow);
	testButtons->addWidget(testDelayed);
	testButtons->addStretch();
	layout->addRow(testButtons);

	connect(announceLevel, &QComboBox::currentIndexChanged, this, &AccessibilitySettingsWidget::changed);
	connect(announceOutputs, &QCheckBox::toggled, this, &AccessibilitySettingsWidget::changed);
	connect(announceStatusMessages, &QCheckBox::toggled, this, &AccessibilitySettingsWidget::changed);
	connect(speechBackend, &QComboBox::currentIndexChanged, this, &AccessibilitySettingsWidget::changed);

	connect(testNow, &QPushButton::clicked, this, [this]() { TestAnnouncement(0); });
	connect(testDelayed, &QPushButton::clicked, this, [this]() { TestAnnouncement(delayedTestMs); });

	UpdateSpeechBackendDescription();
}

void AccessibilitySettingsWidget::Load()
{
	QSignalBlocker blockLevel(announceLevel);
	QSignalBlocker blockOutputs(announceOutputs);
	QSignalBlocker blockStatusMessages(announceStatusMessages);
	QSignalBlocker blockBackend(speechBackend);

	announceLevel->setCurrentIndex(static_cast<int>(Settings::GetAnnounceLevel()));
	announceOutputs->setChecked(Settings::GetAnnounceOutputs());
	announceStatusMessages->setChecked(Settings::GetAnnounceStatusMessages());
	speechBackend->setCurrentIndex(static_cast<int>(Settings::GetSpeechBackend()));

	UpdateSpeechBackendDescription();
}

void AccessibilitySettingsWidget::Save()
{
	Settings::SetAnnounceLevel(static_cast<AnnounceLevel>(announceLevel->currentIndex()));
	Settings::SetAnnounceOutputs(announceOutputs->isChecked());
	Settings::SetAnnounceStatusMessages(announceStatusMessages->isChecked());
	Settings::SetSpeechBackend(static_cast<SpeechBackend>(speechBackend->currentIndex()));
}

void AccessibilitySettingsWidget::UpdateSpeechBackendDescription()
{
	Announcer *announcer = Announcer::Get();

	const char *statusKey = "Accessibility.Settings.NvdaController.NotFound";
	if (announcer && announcer->IsNvdaControllerRunning()) {
		statusKey = "Accessibility.Settings.NvdaController.Running";
	} else if (announcer && announcer->IsNvdaControllerLoaded()) {
		statusKey = "Accessibility.Settings.NvdaController.NotRunning";
	}

	speechBackend->setAccessibleDescription(QTStr(statusKey));
	speechBackend->setToolTip(QTStr(statusKey));
}

void AccessibilitySettingsWidget::TestAnnouncement(int delayMs)
{
	/* Test with the backend currently selected in the dialog, even if the
	 * settings have not been applied yet. */
	SpeechBackend selectedBackend = static_cast<SpeechBackend>(speechBackend->currentIndex());

	QTimer::singleShot(delayMs, this, [selectedBackend]() {
		if (Announcer *announcer = Announcer::Get()) {
			announcer->TestBackend(QTStr("Accessibility.Announce.Test"), selectedBackend);
		}
	});
}

} // namespace OBS::Accessibility
