#include "FrameList.h"
#include "Session.h"

#include <winrt/Windows.Graphics.Holographic.h>
using namespace winrt::Windows::Graphics::Holographic;

#include <winrt/Windows.Perception.h>
using namespace winrt::Windows::Perception;

#include <winrt/Windows.Foundation.h>
using namespace winrt::Windows::Foundation;

#include <winrt/Windows.Perception.Spatial.h>
using namespace winrt::Windows::Perception::Spatial;

// Should be at least ovrMaxProvidedFrameStats or larger
#define MAX_FRAME_HISTORY 5

FrameList::FrameList(HolographicSpace space)
	: m_space(space)
	, m_next_index(0)
	, m_submitted_index(0)
{
	BeginFrame(0);
}

HolographicFrame FrameList::GetFrame(long long frameIndex)
{
	if (frameIndex <= 0)
		frameIndex = m_submitted_index + 1;

	if (frameIndex >= m_next_index)
	{
		BeginFrame(frameIndex);
		std::shared_lock<std::shared_mutex> lk(m_frame_mutex);
		return m_frames.back().second;
	}
	else
	{
		std::shared_lock<std::shared_mutex> lk(m_frame_mutex);
		if (frameIndex < m_frames.front().first)
			return nullptr;

		auto it = m_frames.rbegin();
		while (it != m_frames.rend() && it->first > frameIndex)
			it++;
		return it->second;
	}
}

HolographicFrame FrameList::GetPendingFrame(long long frameIndex)
{
	if (frameIndex > 0 && frameIndex <= m_submitted_index)
		return nullptr;

	return GetFrame(frameIndex);
}

HolographicFrame FrameList::GetFrameAtTime(double absTime)
{
	std::shared_lock<std::shared_mutex> lk(m_frame_mutex);
	if (m_frames.empty())
		return nullptr;

	DateTime target(TimeSpan((int64_t)(absTime * 1.0e+7)));
	for (auto it = m_frames.rbegin(); it != m_frames.rend(); it++)
	{
		HolographicFramePrediction pred = it->second.CurrentPrediction();
		PerceptionTimestamp timestamp = pred.Timestamp();
		DateTime time = timestamp.TargetTime();
		TimeSpan duration = it->second.Duration();

		// Because of rounding errors we need some reasonable timespan
		// to consider two timestamps to refer to the same frame.
		// Here we take half the duration of the frame, if it is outside
		// this timespan it can't possibly refer to the same frame.
		if (abs(target - time) < duration / 2)
			return it->second;
	}
	return nullptr;
}

void FrameList::BeginFrame(long long frameIndex)
{
	if (frameIndex <= 0)
		frameIndex = m_next_index;

	std::unique_lock<std::shared_mutex> lk(m_frame_mutex);
	for (; m_next_index <= frameIndex; m_next_index++)
		m_frames.push_back(Frame(m_next_index, m_space.CreateNextFrame()));
}

void FrameList::EndFrame(long long frameIndex)
{
	std::unique_lock<std::shared_mutex> lk(m_frame_mutex);
	if (frameIndex <= 0)
		frameIndex = m_next_index - 1;

	// Remember the index of the last submitted frame
	m_submitted_index = frameIndex;

	// Clean up old frames that are too old to keep in the cache
	while (m_frames.size() > MAX_FRAME_HISTORY && m_frames.front().first <= frameIndex)
		m_frames.pop_front();
}

void FrameList::Clear()
{
	std::unique_lock<std::shared_mutex> lk(m_frame_mutex);
	m_frames.clear();
	m_next_index = 0;
	m_submitted_index = 0;
}

HolographicCameraPose FrameList::GetPose(long long frameIndex, uint32_t displayIndex)
{
	HolographicFrame frame = GetFrame(frameIndex);
	HolographicFramePrediction prediction = frame.CurrentPrediction();
	return prediction.CameraPoses().GetAt(displayIndex);
}
