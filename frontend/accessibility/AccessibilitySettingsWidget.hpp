#pragma once

#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QPushButton;
class QSpinBox;

namespace OBS::Accessibility {

/* Screen reader options shown on the Accessibility page of the Settings
 * dialog. The dialog calls Load() and Save() and listens to changed(). */
class AccessibilitySettingsWidget : public QGroupBox {
	Q_OBJECT

public:
	explicit AccessibilitySettingsWidget(QWidget *parent = nullptr);

	void Load();
	void Save();

signals:
	void changed();

private:
	void UpdateSpeechBackendDescription();
	void TestAnnouncement(int delayMs);

	QCheckBox *startInAccessibleMode = nullptr;
	QComboBox *announceLevel = nullptr;
	QCheckBox *announceOutputs = nullptr;
	QCheckBox *announceStatusMessages = nullptr;
	QCheckBox *announceWarnings = nullptr;
	QSpinBox *frameLossThreshold = nullptr;
	QComboBox *speechBackend = nullptr;
	QPushButton *testNow = nullptr;
	QPushButton *testDelayed = nullptr;
};

} // namespace OBS::Accessibility
