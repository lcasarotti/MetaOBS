#pragma once

#include "FaceDetection.hpp"
#include "FramingAnalysis.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QElapsedTimer>
#include <QObject>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class OBSBasic;
class QTimer;

namespace OBS::Accessibility {

/* Tells whether the face of the person in front of a camera is centered,
 * how far from the camera they are and how bright the picture is. It speaks
 * once on request, or keeps guiding the person for a while, speaking each
 * time the framing changes, until they stop it. It examines a source's own
 * picture, with its filters but not its position in the scene. Nothing runs
 * until the user asks. Windows only. */
class FramingCheck : public QObject {
	Q_OBJECT

public:
	explicit FramingCheck(OBSBasic *main);
	~FramingCheck() override;

	static FramingCheck *Get();

	void Check(obs_source_t *source);
	void ToggleGuidance(obs_source_t *source);

private:
	enum class Mode {
		Invalid = 0,
		Idle,
		Check,
		Guidance,
	};

	/* The steps the graphics thread goes through to read a picture back,
	 * one per frame so that none waits for the GPU. */
	enum class GrabStage {
		Invalid = 0,
		Idle,
		Render,
		Download,
		Read,
	};

	static void OnTick(void *data, float seconds);
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	void Start(obs_source_t *source, Mode newMode);
	void Stop();
	void RequestPicture();
	void GrabStep();
	bool RenderSource();
	void ReadPicture(uint64_t id);
	void OnResult(uint64_t id, bool pictureRead, bool available, const std::vector<FaceBox> &faces,
		      double meanLuma);
	void OnGuidanceTick();
	QString Describe(const FramingAdvice &advice) const;

	void HoldShowing(obs_source_t *source);
	void ReleaseShowing();
	OBSSource HotkeySource() const;

	void RegisterHotkeys();
	void LoadBindings();
	void UnregisterHotkeys();

	OBSBasic *main;
	std::unique_ptr<FaceDetection> detection;

	Mode mode = Mode::Idle;
	OBSWeakSource lastSource;
	OBSSource shownSource;
	bool pictureRequested = false;
	/* Tells the picture asked for last from older ones. */
	uint64_t requestId = 0;
	QTimer *guidanceTimer;
	QElapsedTimer guidanceClock;
	std::optional<FramingAdvice> candidate;
	int candidateCount = 0;
	std::optional<FramingAdvice> announced;

	/* Shared with the graphics thread. */
	std::mutex grabMutex;
	OBSWeakSource grabSource;
	GrabStage grabStage = GrabStage::Idle;
	uint64_t grabRequestId = 0;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurf = nullptr;
	uint32_t grabWidth = 0;
	uint32_t grabHeight = 0;

	obs_hotkey_id checkHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id guidanceHotkey = OBS_INVALID_HOTKEY_ID;
};

} // namespace OBS::Accessibility
