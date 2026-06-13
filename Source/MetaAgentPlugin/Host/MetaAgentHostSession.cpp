#include "Host/MetaAgentHostSession.h"

#include "Host/MetaAgentHttpBridge.h"
#include "MetaAgentPlayerController.h"
#include "MetaAgentPlugin.h"

namespace
{
std::string ToCoreString(const FString& Value)
{
	const FTCHARToUTF8 Converter(*Value);
	return std::string(Converter.Get(), static_cast<size_t>(Converter.Length()));
}
}

metaagent::session::RuntimeSession FMetaAgentHostSessionSnapshot::ToCoreSession() const
{
	metaagent::session::RuntimeSession Session;
	Session.active = bActive;
	Session.features.input = bInputEnabled;
	Session.features.camera = bCameraEnabled;
	Session.features.ai = bAiEnabled;
	Session.features.networking = bNetworkingEnabled;
	Session.features.recording = bRecordingEnabled;
	Session.features.ui = bUiEnabled;
	Session.features.particle = bParticleEnabled;
	Session.map_name = ToCoreString(MapName);
	Session.build_label = ToCoreString(BuildLabel);
	Session.http_port = HttpPort;
	Session.http_enabled = bHttpEnabled;
	Session.http_router_bound = bHttpRouterBound;
	return Session;
}

FString MetaAgentHostSession::BuildLabelFromBuildConfig()
{
#if UE_BUILD_SHIPPING
	return TEXT("Shipping");
#elif UE_BUILD_DEVELOPMENT
	return TEXT("Development");
#elif UE_BUILD_DEBUG
	return TEXT("Debug");
#else
	return TEXT("Other");
#endif
}

FMetaAgentHostSessionSnapshot MetaAgentHostSession::MakeFromWorld(
	const UWorld* World,
	const bool bActive,
	const bool bInputEnabled,
	const bool bCameraEnabled,
	const bool bAiEnabled,
	const bool bNetworkingEnabled,
	const bool bRecordingEnabled,
	const bool bUiEnabled,
	const bool bParticleEnabled,
	const int32 HttpPort,
	const bool bHttpEnabled,
	const bool bHttpRouterBound)
{
	FMetaAgentHostSessionSnapshot Snapshot;
	Snapshot.bActive = bActive;
	Snapshot.bInputEnabled = bInputEnabled;
	Snapshot.bCameraEnabled = bCameraEnabled;
	Snapshot.bAiEnabled = bAiEnabled;
	Snapshot.bNetworkingEnabled = bNetworkingEnabled;
	Snapshot.bRecordingEnabled = bRecordingEnabled;
	Snapshot.bUiEnabled = bUiEnabled;
	Snapshot.bParticleEnabled = bParticleEnabled;
	Snapshot.MapName = World ? World->GetMapName() : TEXT("unknown");
	Snapshot.BuildLabel = BuildLabelFromBuildConfig();
	Snapshot.HttpPort = HttpPort;
	Snapshot.bHttpEnabled = bHttpEnabled;
	Snapshot.bHttpRouterBound = bHttpRouterBound;
	return Snapshot;
}

FMetaAgentHostSessionSnapshot MetaAgentHostSession::MakeFromPlayerController(
	const AMetaAgentPlayerController& Controller)
{
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	const UWorld* World = Controller.GetWorld();

	return MakeFromWorld(
		World,
		IsMetaAgentRuntimeActive(),
		!Settings || Settings->bEnableInputSystems,
		Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::Camera),
		Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI),
		Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::Networking),
		Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording),
		!Settings || Settings->bEnableUISystems,
		Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle),
		Settings ? Settings->LocalHttpServerPort : 0,
		Settings && Settings->bEnableNetworkingSystems,
		FMetaAgentHttpBridge::Get().IsRouterBound());
}
