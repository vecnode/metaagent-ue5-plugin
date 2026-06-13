#pragma once

#include "CoreMinimal.h"
#include "Host/MetaAgentHostSession.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"

class IHttpRouter;
struct FHttpServerRequest;

class FMetaAgentHttpBridge
{
public:
	struct FCallbacks
	{
		TFunction<FMetaAgentHostSessionSnapshot()> BuildSessionSnapshot;
		TFunction<void(const FString& Message)> OnNotifyMessage;
	};

	static FMetaAgentHttpBridge& Get();

	bool IsRouterBound() const { return LocalHttpRouter.IsValid(); }

	FString GetStatusText(const FMetaAgentHostSessionSnapshot& SessionSnapshot) const;

	bool Start(int32 Port, bool bEnabled, const FCallbacks& Callbacks);

	void Stop();

private:
	bool HandleHttpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	FCallbacks ActiveCallbacks;
	TSharedPtr<IHttpRouter> LocalHttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
};
