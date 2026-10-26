#pragma once

#include "FramingAnalysis.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace OBS::Accessibility {

/* Finds faces in grayscale pictures with the face detector built into
 * Windows, on a thread of its own: the detector's calls block, and must not
 * run on the UI thread. Windows only. */
class FaceDetection {
public:
	struct Picture {
		std::vector<uint8_t> gray;
		int width = 0;
		int height = 0;
	};

	/* Called on the detection thread. available is false when this
	 * version of Windows has no face detector. */
	using Callback = std::function<void(bool available, const std::vector<FaceBox> &faces)>;

	FaceDetection();
	~FaceDetection();

	/* Replaces the picture waiting to be examined, if any: only the most
	 * recent one matters. */
	void Detect(Picture picture, Callback callback);

private:
	void Run();

	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	bool stopping = false;
	bool pending = false;
	Picture nextPicture;
	Callback nextCallback;
};

} // namespace OBS::Accessibility
