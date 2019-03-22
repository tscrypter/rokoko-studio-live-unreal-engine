// Copyright 2019 Rokoko Electronics. All Rights Reserved.

#include "VPStreamingNetwork.h"
#include "VirtualProductionFrame.h"
#include "Smartsuit.h"
#include "Runtime/Core/Public/Containers/UnrealString.h"
#include "Engine.h"
#include "Runtime/JsonUtilities/Public/JsonObjectConverter.h"


VPStreamingNetwork::VPStreamingNetwork()
{
}


VPStreamingNetwork::~VPStreamingNetwork()
{
	// Stop the runnable
	Stop();
	FVirtualProductionSource* livelink = FVirtualProductionSource::Get();
	if (livelink) {
		for (int i = 0; i < subjectNames.Num(); i++) {
			livelink->HandleClearSubject(subjectNames[i]);
		}
	}
	//livelink->HandleClearSubject(FLiveLinkClearSubject(FString("subjectNames")));

	if (Socket) {
		Socket->Close();
	}

	// And last but not least stop the main thread
	if (Thread != NULL)
	{
		Thread->Kill(true);
		delete Thread;
	}
}


void VPStreamingNetwork::Start(int port) {
	streaming_port = port;
	FString ThreadName(FString::Printf(TEXT("VPStreamingNetwork%ld"), (long)(FDateTime::UtcNow().ToUnixTimestamp())));
	if (streaming_port) {
		UE_LOG(LogTemp, Warning, TEXT("VP port... %i"), streaming_port);
	}
	Thread = FRunnableThread::Create(this, *ThreadName, 8 * 1024, TPri_Normal);
}


bool VPStreamingNetwork::Init()
{
	if (Socket == NULL)
	{
		Socket = FUdpSocketBuilder(TEXT("VPStreamingNetwork")).BoundToAddress(FIPv4Address(0, 0, 0, 0)).BoundToPort(streaming_port).AsReusable().Build();
		//allow the socket to listen to an already bounded address.
		Socket->SetReuseAddr(true);
		Stopping = false;
	}
	return true;
}

FString BytesToStringFixed(const uint8 *In, int32 Count)
{
	FString Broken = BytesToString(In, Count);
	FString Fixed;

	for (int i = 0; i < Broken.Len(); i++)
	{
		const TCHAR c = Broken[i] - 1;
		Fixed.AppendChar(c);
	}

	return Fixed;
}

void VPStreamingNetwork::SendToLiveLink(FVirtualProductionSubject movable) {
	FVirtualProductionSource* livelink = FVirtualProductionSource::Get();
	if (livelink) {
		livelink->HandleSubjectFrame(movable);
	}
}

uint32 VPStreamingNetwork::Run()
{
	bool added = false;
	while (!Stopping)
	{
		auto addr_in = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		int32 bytes_read = 0;
		FString ret;

		uint8 data[8192];
		memset(data, '\0', 8192);

		FDateTime time = FDateTime::UtcNow();
		int seconds = time.ToUnixTimestamp();
		if (Socket) {
			if (Socket->RecvFrom(data, sizeof(data), bytes_read, *addr_in)) {
				if (bytes_read == 0) {
					continue;
				}
				//FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				//{
					if (Stopping) break;
					mtx.lock();
					FVirtualProductionFrame VPFrame;
					FString result = BytesToStringFixed(data, static_cast<int32_t>(bytes_read));
					FJsonObjectConverter::JsonObjectStringToUStruct(result, &VPFrame, 0, 0);

					GlobalVPFrame = new FVirtualProductionFrame();
					GlobalVPFrame->version = VPFrame.version;
					for (int i = 0; i < VPFrame.props.Num(); i++) {
						GlobalVPFrame->props.Add(VPFrame.props[i]);
					}
					for (int i = 0; i < VPFrame.trackers.Num(); i++) {
						GlobalVPFrame->trackers.Add(VPFrame.trackers[i]);
					}
					FVirtualProductionSource* livelink = FVirtualProductionSource::Get();
					if (livelink) {
						
						
						/*SubjectData->RefSkeleton = Subject.RefSkeleton;
						SubjectData->SubjectName = SubjectName;
*/
						//message.RefSkeleton.SetBoneNames()
						if (!added) {
							added = true;
							for (int i = 0; i < VPFrame.props.Num(); i++) {
								livelink->HandleSubjectData(VPFrame.props[i].GetSubject());
								subjectNames.Add(VPFrame.props[i].GetSubject().name);
							}
							for (int i = 0; i < VPFrame.trackers.Num(); i++) {
								livelink->HandleSubjectData(VPFrame.trackers[i].GetSubject());
								subjectNames.Add(VPFrame.trackers[i].GetSubject().name);
							}
						}

						for (int i = 0; i < VPFrame.props.Num(); i++) {
							SendToLiveLink(VPFrame.props[i].GetSubject());
							
						}
						for (int i = 0; i < VPFrame.trackers.Num(); i++) {
							SendToLiveLink(VPFrame.trackers[i].GetSubject());
						}

						TArray<FLiveLinkCurveElement> frame;
						//FLiveLinkCurveElement curve;
						//curve.CurveName = "x";
						//curve.CurveValue = 1;
						//frame.AddUnique(curve)
						//const FName camera2("Camera-2");
						//livelink->HandleSubjectFrame(camera2, frame);
						
						UE_LOG(LogTemp, Warning, TEXT("I see livelink!!"));
					}
					else {
						UE_LOG(LogTemp, Warning, TEXT("no livelink!!"));
					}
					mtx.unlock();
				//}, TStatId(), NULL, ENamedThreads::GameThread);

				// If you want to wait for the code above to complete do this:
				//FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

			}
		}
	}
	return 0;
}

FProp* VPStreamingNetwork::GetPropByName(FString name, bool isLive)
{
	FProp *result = nullptr;
	mtx.lock();
	if (GlobalVPFrame) {
		for (int i = 0; i < GlobalVPFrame->props.Num(); i++) {
			if (name == GlobalVPFrame->props[i].name && GlobalVPFrame->props[i].isLive == isLive)
			{
				result = &GlobalVPFrame->props[i];
			}
		}
	}
	mtx.unlock();
	return result;
}

FTracker* VPStreamingNetwork::GetTrackerByName(FString name, bool isLive)
{
	FTracker *result = nullptr;
	mtx.lock();
	if (GlobalVPFrame) {
		for (int i = 0; i < GlobalVPFrame->trackers.Num(); i++) {
			if (name == GlobalVPFrame->trackers[i].name && GlobalVPFrame->trackers[i].isLive == isLive)
			{
				result = &GlobalVPFrame->trackers[i];
			}
		}
	}
	mtx.unlock();
	return result;
}