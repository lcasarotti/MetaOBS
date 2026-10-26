#include "FramingCheck.hpp"
#include "AccessibleCommon.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QTimer>

#include <algorithm>
#include <cstring>
#include <utility>

#include "moc_FramingCheck.cpp"

namespace OBS::Accessibility {

namespace {
FramingCheck *instance = nullptr;

constexpr const char *checkHotkeyName = "Accessibility.CheckFraming";
constexpr const char *guidanceHotkeyName = "Accessibility.FramingGuidance";

/* The face detector works as well on a small picture, and faster. */
constexpr uint32_t pictureWidth = 640;

/* A camera that was not showing needs a moment to start. */
constexpr int cameraStartDelay = 1500;

constexpr int guidanceInterval = 500;
constexpr qint64 guidanceDuration = 2 * 60 * 1000;
/* The same advice must come twice in a row before it is spoken, so a face
 * on the edge of a zone does not make the guidance chatter. */
constexpr int stableResults = 2;

QString FirstUpper(QString text)
{
	if (!text.isEmpty()) {
		text[0] = text[0].toUpper();
	}
	return text;
}

QString MoveText(FramingMove move, FramingAmount amount)
{
	bool large = amount == FramingAmount::Large;
	switch (move) {
	case FramingMove::Left:
		return QTStr(large ? "Accessibility.Framing.MoveLeft.Large" : "Accessibility.Framing.MoveLeft");
	case FramingMove::Right:
		return QTStr(large ? "Accessibility.Framing.MoveRight.Large" : "Accessibility.Framing.MoveRight");
	case FramingMove::FaceTooHigh:
		return QTStr(large ? "Accessibility.Framing.TooHigh.Large" : "Accessibility.Framing.TooHigh");
	case FramingMove::FaceTooLow:
		return QTStr(large ? "Accessibility.Framing.TooLow.Large" : "Accessibility.Framing.TooLow");
	default:
		return {};
	}
}

QString LightText(FramingLight light)
{
	switch (light) {
	case FramingLight::Black:
		return QTStr("Accessibility.Framing.Light.Black");
	case FramingLight::VeryDark:
		return QTStr("Accessibility.Framing.Light.VeryDark");
	case FramingLight::Dark:
		return QTStr("Accessibility.Framing.Light.Dark");
	case FramingLight::VeryBright:
		return QTStr("Accessibility.Framing.Light.VeryBright");
	default:
		return {};
	}
}

/* What was found, for the log users send when the advice seems wrong. */
QString BoxText(const FaceBox &box)
{
	return QString("%1,%2 %3x%4")
		.arg(box.left, 0, 'f', 2)
		.arg(box.top, 0, 'f', 2)
		.arg(box.width, 0, 'f', 2)
		.arg(box.height, 0, 'f', 2);
}

void LogResult(const std::vector<FaceBox> &faces, const FaceBox &window, double meanLuma)
{
	QStringList boxes;
	for (const FaceBox &face : faces) {
		boxes << BoxText(face);
	}
	blog(LOG_INFO, "[Accessibility] Framing: brightness %.0f, window %s, %d face(s) %s", meanLuma,
	     QT_TO_UTF8(BoxText(window)), (int)faces.size(), QT_TO_UTF8(boxes.join("; ")));
}

/* Below this share of the picture, the scene shows only part of it. */
constexpr double fullWindowShare = 0.95;

Affine2D ToAffine(const matrix4 &transform)
{
	vec3 origin;
	vec3 unitX;
	vec3 unitY;
	vec3_set(&origin, 0.0f, 0.0f, 0.0f);
	vec3_set(&unitX, 1.0f, 0.0f, 0.0f);
	vec3_set(&unitY, 0.0f, 1.0f, 0.0f);
	vec3_transform(&origin, &origin, &transform);
	vec3_transform(&unitX, &unitX, &transform);
	vec3_transform(&unitY, &unitY, &transform);

	Affine2D affine;
	affine.xx = unitX.x - origin.x;
	affine.xy = unitX.y - origin.y;
	affine.yx = unitY.x - origin.x;
	affine.yy = unitY.y - origin.y;
	affine.x0 = origin.x;
	affine.y0 = origin.y;
	return affine;
}

double AlignmentShare(uint32_t alignment, uint32_t start, uint32_t end)
{
	if (alignment & start) {
		return 0.0;
	}
	return (alignment & end) ? 1.0 : 0.5;
}

/* Items in a group are placed relative to the group. */
ItemPlacement DescribePlacement(obs_sceneitem_t *item, obs_sceneitem_t *group)
{
	matrix4 draw;
	matrix4 box;
	obs_sceneitem_get_draw_transform(item, &draw);
	obs_sceneitem_get_box_transform(item, &box);
	if (group) {
		matrix4 groupTransform;
		obs_sceneitem_get_draw_transform(group, &groupTransform);
		matrix4_mul(&draw, &draw, &groupTransform);
		matrix4_mul(&box, &box, &groupTransform);
	}

	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);

	obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t alignment = obs_sceneitem_get_bounds_alignment(item);

	ItemPlacement placement;
	placement.sourceWidth = obs_source_get_width(source);
	placement.sourceHeight = obs_source_get_height(source);
	placement.cropLeft = crop.left;
	placement.cropTop = crop.top;
	placement.cropRight = crop.right;
	placement.cropBottom = crop.bottom;
	placement.draw = ToAffine(draw);
	placement.canvasWidth = ovi.base_width;
	placement.canvasHeight = ovi.base_height;
	placement.cropToBox = obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE &&
			      obs_sceneitem_get_bounds_crop(item);
	placement.box = ToAffine(box);
	placement.boxAlignX = AlignmentShare(alignment, OBS_ALIGN_LEFT, OBS_ALIGN_RIGHT);
	placement.boxAlignY = AlignmentShare(alignment, OBS_ALIGN_TOP, OBS_ALIGN_BOTTOM);
	return placement;
}

bool IsCamera(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	return id && strcmp(id, "dshow_input") == 0;
}

/* Enumeration callback: stops at the first visible camera, looking into
 * groups too. */
bool FindCameraItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto camera = static_cast<obs_sceneitem_t **>(param);
	if (!obs_sceneitem_visible(item)) {
		return true;
	}

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, FindCameraItem, param);
		return *camera == nullptr;
	}

	if (IsCamera(obs_sceneitem_get_source(item))) {
		*camera = item;
		return false;
	}
	return true;
}

obs_sceneitem_t *FindCamera(obs_scene_t *scene)
{
	obs_sceneitem_t *camera = nullptr;
	obs_scene_enum_items(scene, FindCameraItem, &camera);
	return camera;
}
} // namespace

FramingCheck::FramingCheck(OBSBasic *main_)
	: QObject(main_),
	  main(main_),
	  detection(std::make_unique<FaceDetection>())
{
	instance = this;

	guidanceTimer = new QTimer(this);
	guidanceTimer->setInterval(guidanceInterval);
	connect(guidanceTimer, &QTimer::timeout, this, &FramingCheck::OnGuidanceTick);

	obs_add_tick_callback(OnTick, this);
	RegisterHotkeys();
	obs_frontend_add_event_callback(OnFrontendEvent, this);
}

FramingCheck::~FramingCheck()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	UnregisterHotkeys();
	obs_remove_tick_callback(OnTick, this);

	/* Waits for the detection under way, whose result is then dropped. */
	detection.reset();
	ReleaseShowing();

	obs_enter_graphics();
	gs_stagesurface_destroy(stagesurf);
	gs_texrender_destroy(texrender);
	obs_leave_graphics();

	instance = nullptr;
}

FramingCheck *FramingCheck::Get()
{
	return instance;
}

void FramingCheck::Check(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (!scene || !item) {
		AnnounceRequested(QTStr("Accessibility.Framing.NoCamera"));
		return;
	}

	/* While guiding, the latest advice is already known. */
	if (mode == Mode::Guidance && IsTarget(item) && announced) {
		Placement placement = CurrentPlacement();
		AnnounceRequested(Describe(*announced, &placement));
		return;
	}

	Start(scene, item, Mode::Check);
}

void FramingCheck::ToggleGuidance(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (mode == Mode::Guidance) {
		Stop();
		AnnounceRequested(QTStr("Accessibility.Framing.GuidanceOff"));
		return;
	}

	if (!scene || !item) {
		AnnounceRequested(QTStr("Accessibility.Framing.NoCamera"));
		return;
	}

	AnnounceRequested(QTStr("Accessibility.Framing.GuidanceOn"));
	Start(scene, item, Mode::Guidance);
}

void FramingCheck::Start(obs_scene_t *scene, obs_sceneitem_t *item, Mode newMode)
{
	Stop();

	mode = newMode;
	target.scene = OBSGetWeakRef(obs_scene_get_source(scene));
	target.parent = OBSGetWeakRef(obs_scene_get_source(obs_sceneitem_get_scene(item)));
	target.itemId = obs_sceneitem_get_id(item);

	obs_source_t *source = obs_sceneitem_get_source(item);
	bool wasShowing = obs_source_showing(source);
	HoldShowing(source);

	int delay = wasShowing ? 0 : cameraStartDelay;
	if (mode == Mode::Guidance) {
		guidanceClock.start();
		QTimer::singleShot(delay, this, [this]() {
			if (mode == Mode::Guidance) {
				guidanceTimer->start();
			}
		});
	} else {
		QTimer::singleShot(delay, this, [this]() {
			if (mode == Mode::Check) {
				RequestPicture();
			}
		});
	}
}

bool FramingCheck::IsTarget(obs_sceneitem_t *item) const
{
	OBSSourceAutoRelease parent = OBSGetStrongRef(target.parent);
	return parent && obs_scene_get_source(obs_sceneitem_get_scene(item)) == parent.Get() &&
	       obs_sceneitem_get_id(item) == target.itemId;
}

FramingCheck::Placement FramingCheck::CurrentPlacement() const
{
	Placement placement;

	OBSSourceAutoRelease sceneSource = OBSGetStrongRef(target.scene);
	OBSSourceAutoRelease parentSource = OBSGetStrongRef(target.parent);
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_scene_t *parent = obs_group_or_scene_from_source(parentSource);
	obs_sceneitem_t *item = parent ? obs_scene_find_sceneitem_by_id(parent, target.itemId) : nullptr;
	if (!scene || !item) {
		return placement;
	}

	obs_sceneitem_t *group = obs_sceneitem_get_group(scene, item);
	placement.hidden = !obs_sceneitem_visible(item) || (group && !obs_sceneitem_visible(group));

	ItemPlacement itemPlacement = DescribePlacement(item, group);
	FaceBox window = VisibleWindow(itemPlacement);
	if (window.width <= 0.0 || window.height <= 0.0) {
		placement.offCanvas = true;
		return placement;
	}

	placement.window = window;
	placement.partial = window.width * window.height < fullWindowShare;
	return placement;
}

void FramingCheck::Stop()
{
	mode = Mode::Idle;
	guidanceTimer->stop();
	candidate.reset();
	candidateCount = 0;
	announced.reset();

	{
		std::lock_guard lock(grabMutex);
		grabSource = nullptr;
		grabStage = GrabStage::Idle;
	}
	/* A picture under way belongs to what was stopped. */
	pictureRequested = false;
	requestId++;

	ReleaseShowing();
}

void FramingCheck::HoldShowing(obs_source_t *source)
{
	ReleaseShowing();
	obs_source_inc_showing(source);
	shownSource = source;
}

void FramingCheck::ReleaseShowing()
{
	if (shownSource) {
		obs_source_dec_showing(shownSource);
		shownSource = nullptr;
	}
}

void FramingCheck::RequestPicture()
{
	if (pictureRequested || !shownSource) {
		return;
	}

	pictureRequested = true;
	requestId++;

	std::lock_guard lock(grabMutex);
	grabSource = OBSGetWeakRef(shownSource);
	grabRequestId = requestId;
	grabStage = GrabStage::Render;
}

void FramingCheck::OnTick(void *data, float)
{
	auto check = static_cast<FramingCheck *>(data);
	check->GrabStep();
}

/* Runs on the graphics thread. */
void FramingCheck::GrabStep()
{
	std::lock_guard lock(grabMutex);
	if (grabStage == GrabStage::Idle) {
		return;
	}

	uint64_t id = grabRequestId;
	obs_enter_graphics();
	switch (grabStage) {
	case GrabStage::Render:
		if (RenderSource()) {
			grabStage = GrabStage::Download;
		} else {
			grabStage = GrabStage::Idle;
			QMetaObject::invokeMethod(
				this, [this, id]() { OnResult(id, false, true, {}, 0.0); }, Qt::QueuedConnection);
		}
		break;
	case GrabStage::Download:
		gs_stage_texture(stagesurf, gs_texrender_get_texture(texrender));
		grabStage = GrabStage::Read;
		break;
	case GrabStage::Read:
		ReadPicture(id);
		grabStage = GrabStage::Idle;
		break;
	default:
		break;
	}
	obs_leave_graphics();
}

/* Renders the source, with its filters, into a small SDR picture. */
bool FramingCheck::RenderSource()
{
	OBSSourceAutoRelease source = OBSGetStrongRef(grabSource);
	uint32_t sourceWidth = source ? obs_source_get_width(source) : 0;
	uint32_t sourceHeight = source ? obs_source_get_height(source) : 0;
	if (!sourceWidth || !sourceHeight) {
		return false;
	}

	uint32_t width = std::min(pictureWidth, sourceWidth) & ~1u;
	uint32_t height = std::max(2u, (uint32_t)((uint64_t)width * sourceHeight / sourceWidth) & ~1u);

	if (!texrender) {
		texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	}
	if (!stagesurf || width != grabWidth || height != grabHeight) {
		gs_stagesurface_destroy(stagesurf);
		stagesurf = gs_stagesurface_create(width, height, GS_RGBA);
		grabWidth = width;
		grabHeight = height;
	}

	gs_texrender_reset(texrender);
	if (!gs_texrender_begin_with_color_space(texrender, width, height, GS_CS_SRGB)) {
		return false;
	}

	vec4 black;
	vec4_zero(&black);
	gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
	gs_ortho(0.0f, (float)sourceWidth, 0.0f, (float)sourceHeight, -100.0f, 100.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(source);
	gs_blend_state_pop();

	gs_texrender_end(texrender);
	return true;
}

/* Runs on the graphics thread. The face detection runs on its own. */
void FramingCheck::ReadPicture(uint64_t id)
{
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stagesurf, &data, &linesize)) {
		QMetaObject::invokeMethod(
			this, [this, id]() { OnResult(id, false, true, {}, 0.0); }, Qt::QueuedConnection);
		return;
	}

	FaceDetection::Picture picture;
	picture.width = (int)grabWidth;
	picture.height = (int)grabHeight;
	picture.gray.resize((size_t)grabWidth * grabHeight);
	RgbaToGray(data, picture.width, picture.height, (int)linesize, picture.gray.data());
	gs_stagesurface_unmap(stagesurf);

	double meanLuma = MeanLuma(picture.gray.data(), picture.width, picture.height, picture.width);

	detection->Detect(std::move(picture), [this, id, meanLuma](bool available, const std::vector<FaceBox> &faces) {
		QMetaObject::invokeMethod(
			this, [this, id, available, faces, meanLuma]() { OnResult(id, true, available, faces, meanLuma); },
			Qt::QueuedConnection);
	});
}

void FramingCheck::OnResult(uint64_t id, bool pictureRead, bool available, const std::vector<FaceBox> &faces,
			    double meanLuma)
{
	if (!pictureRequested || id != requestId) {
		return;
	}
	pictureRequested = false;

	QString error;
	if (!pictureRead) {
		error = QTStr("Accessibility.Framing.NoPicture");
	} else if (!available) {
		error = QTStr("Accessibility.Framing.Unavailable");
	}

	if (!error.isEmpty()) {
		Stop();
		AnnounceRequested(error);
		return;
	}

	Placement placement = CurrentPlacement();
	FramingAdvice advice = AnalyzeFraming(faces, placement.window, meanLuma, false);

	if (mode == Mode::Check) {
		LogResult(faces, placement.window, meanLuma);
		Stop();
		AnnounceRequested(Describe(advice, &placement));
		return;
	}

	if (mode != Mode::Guidance) {
		return;
	}

	if (candidate && *candidate == advice) {
		candidateCount++;
	} else {
		candidate = advice;
		candidateCount = 1;
	}

	if (candidateCount >= stableResults && (!announced || *announced != advice)) {
		/* What the scene does to the picture is said once. */
		bool first = !announced;
		announced = advice;
		LogResult(faces, placement.window, meanLuma);
		if (Announcer *announcer = Announcer::Get()) {
			announcer->AnnounceAtOnce(Describe(advice, first ? &placement : nullptr));
		}
	}
}

void FramingCheck::OnGuidanceTick()
{
	if (guidanceClock.hasExpired(guidanceDuration)) {
		Stop();
		AnnounceRequested(QTStr("Accessibility.Framing.GuidanceTimeout"));
		return;
	}

	RequestPicture();
}

QString FramingCheck::Describe(const FramingAdvice &advice, const Placement *placement) const
{
	QStringList sentences;

	if (advice.faceCount == 0) {
		sentences << QTStr("Accessibility.Framing.NoFace");
	} else {
		if (advice.faceCount > 1) {
			sentences << QTStr("Accessibility.Framing.ManyFaces").arg(advice.faceCount);
		}

		if (advice.visibility == FramingVisibility::Outside) {
			sentences << QTStr("Accessibility.Framing.Outside");
		} else if (advice.visibility == FramingVisibility::PartlyOutside) {
			sentences << QTStr("Accessibility.Framing.PartlyOutside");
		}

		if (advice.IsCentered()) {
			sentences << QTStr("Accessibility.Framing.Centered");
		} else {
			QStringList moves;
			for (QString text : {MoveText(advice.horizontal, advice.horizontalAmount),
					     MoveText(advice.vertical, advice.verticalAmount)}) {
				if (!text.isEmpty()) {
					moves << text;
				}
			}
			sentences << FirstUpper(moves.join(", "));
		}

		if (advice.distance == FramingDistance::TooFar) {
			sentences << QTStr("Accessibility.Framing.TooFar");
		} else if (advice.distance == FramingDistance::TooClose) {
			sentences << QTStr("Accessibility.Framing.TooClose");
		}
	}

	QString light = LightText(advice.light);
	if (!light.isEmpty()) {
		sentences << light;
	}

	if (placement) {
		if (placement->offCanvas) {
			sentences << QTStr("Accessibility.Framing.OffCanvas");
		} else if (placement->partial) {
			sentences << QTStr("Accessibility.Framing.PartOfPicture");
		}
		if (placement->hidden) {
			sentences << QTStr("Accessibility.Framing.Hidden");
		}
	}

	return sentences.join(". ") + ".";
}

/* The hotkeys work on the item checked last, or else on the first camera of
 * the scene on air. */
void FramingCheck::FindHotkeyTarget(OBSScene &scene, OBSSceneItem &item) const
{
	OBSSourceAutoRelease sceneSource = OBSGetStrongRef(target.scene);
	OBSSourceAutoRelease parentSource = OBSGetStrongRef(target.parent);
	obs_scene_t *parent = obs_group_or_scene_from_source(parentSource);
	if (sceneSource && parent) {
		if (obs_sceneitem_t *last = obs_scene_find_sceneitem_by_id(parent, target.itemId)) {
			scene = obs_scene_from_source(sceneSource);
			item = last;
			return;
		}
	}

	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	if (obs_scene_t *programScene = obs_scene_from_source(program)) {
		scene = programScene;
		item = FindCamera(programScene);
	}
}

void FramingCheck::RegisterHotkeys()
{
	auto describe = [](const char *lookupKey) {
		QString label = QTStr(lookupKey).remove('&');
		return QTStr("Accessibility.Hotkey.Description").arg(label);
	};

	checkHotkey = obs_hotkey_register_frontend(
		checkHotkeyName, QT_TO_UTF8(describe("Accessibility.Framing.Check")), OnHotkey, this);
	guidanceHotkey = obs_hotkey_register_frontend(
		guidanceHotkeyName, QT_TO_UTF8(describe("Accessibility.Framing.Guidance")), OnHotkey, this);
	LoadBindings();
}

/* Settings > Hotkeys saves frontend hotkeys in the profile's [Hotkeys]
 * section, under their names. */
void FramingCheck::LoadBindings()
{
	for (auto [id, name] : {std::pair{checkHotkey, checkHotkeyName}, std::pair{guidanceHotkey, guidanceHotkeyName}}) {
		const char *json = config_get_string(main->Config(), "Hotkeys", name);
		OBSDataAutoRelease data = json ? obs_data_create_from_json(json) : nullptr;
		OBSDataArrayAutoRelease bindings = data ? obs_data_get_array(data, "bindings") : nullptr;
		obs_hotkey_load(id, bindings);
	}
}

void FramingCheck::UnregisterHotkeys()
{
	for (obs_hotkey_id *id : {&checkHotkey, &guidanceHotkey}) {
		if (*id != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(*id);
			*id = OBS_INVALID_HOTKEY_ID;
		}
	}
}

void FramingCheck::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto check = static_cast<FramingCheck *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		check->LoadBindings();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		check->Stop();
		check->target = {};
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		check->Stop();
		check->UnregisterHotkeys();
		break;
	default:
		break;
	}
}

/* The main window routes hotkeys to the UI thread. */
void FramingCheck::OnHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	auto check = static_cast<FramingCheck *>(data);
	if (!pressed) {
		return;
	}

	OBSScene scene;
	OBSSceneItem item;
	check->FindHotkeyTarget(scene, item);

	if (id == check->checkHotkey) {
		check->Check(scene, item);
	} else if (id == check->guidanceHotkey) {
		check->ToggleGuidance(scene, item);
	}
}

} // namespace OBS::Accessibility
