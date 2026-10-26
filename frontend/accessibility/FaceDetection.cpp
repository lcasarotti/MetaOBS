#include "FaceDetection.hpp"

#include <obs.h>

#include <unknwn.h>
#include <MemoryBuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>

#include <cstring>

namespace OBS::Accessibility {

namespace {
using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::FaceAnalysis::DetectedFace;
using winrt::Windows::Media::FaceAnalysis::FaceDetector;

FaceDetector CreateDetector()
{
	try {
		if (FaceDetector::IsSupported()) {
			return FaceDetector::CreateAsync().get();
		}
	} catch (const winrt::hresult_error &error) {
		blog(LOG_WARNING, "[Accessibility] Cannot create the face detector: 0x%08X", (unsigned)error.code());
	}
	return nullptr;
}

SoftwareBitmap ToBitmap(const FaceDetection::Picture &picture)
{
	SoftwareBitmap bitmap(BitmapPixelFormat::Gray8, picture.width, picture.height);

	auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
	auto plane = buffer.GetPlaneDescription(0);
	auto reference = buffer.CreateReference();
	auto access = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();

	uint8_t *data = nullptr;
	uint32_t capacity = 0;
	winrt::check_hresult(access->GetBuffer(&data, &capacity));

	for (int y = 0; y < picture.height; y++) {
		uint8_t *line = data + plane.StartIndex + (size_t)y * plane.Stride;
		memcpy(line, picture.gray.data() + (size_t)y * picture.width, picture.width);
	}

	/* The detector cannot read the bitmap while it is locked. */
	reference.Close();
	buffer.Close();
	return bitmap;
}

std::vector<FaceBox> FindFaces(FaceDetector &detector, const FaceDetection::Picture &picture)
{
	std::vector<FaceBox> faces;

	try {
		for (const DetectedFace &face : detector.DetectFacesAsync(ToBitmap(picture)).get()) {
			auto box = face.FaceBox();
			faces.push_back({(double)box.X / picture.width, (double)box.Y / picture.height,
					 (double)box.Width / picture.width, (double)box.Height / picture.height});
		}
	} catch (const winrt::hresult_error &error) {
		blog(LOG_WARNING, "[Accessibility] Face detection failed: 0x%08X", (unsigned)error.code());
	}

	return faces;
}
} // namespace

FaceDetection::FaceDetection() : thread(&FaceDetection::Run, this) {}

FaceDetection::~FaceDetection()
{
	{
		std::lock_guard lock(mutex);
		stopping = true;
	}
	wake.notify_one();
	thread.join();
}

void FaceDetection::Detect(Picture picture, Callback callback)
{
	{
		std::lock_guard lock(mutex);
		nextPicture = std::move(picture);
		nextCallback = std::move(callback);
		pending = true;
	}
	wake.notify_one();
}

void FaceDetection::Run()
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);

	/* The detector is created on first use, which takes a moment. */
	FaceDetector detector = nullptr;
	bool created = false;

	for (;;) {
		Picture picture;
		Callback callback;
		{
			std::unique_lock lock(mutex);
			wake.wait(lock, [this]() { return stopping || pending; });
			if (stopping) {
				break;
			}
			picture = std::move(nextPicture);
			callback = std::move(nextCallback);
			pending = false;
		}

		if (!created) {
			detector = CreateDetector();
			created = true;
		}

		if (!detector) {
			callback(false, {});
			continue;
		}

		callback(true, FindFaces(detector, picture));
	}

	detector = nullptr;
	winrt::uninit_apartment();
}

} // namespace OBS::Accessibility
