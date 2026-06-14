#include "Host/MetaAgentHttpBridge.h"

#include "MetaAgentPlugin.h"

#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

#include "net/router.hpp"
#include "net/types.hpp"
#include "session/status.hpp"

namespace
{
metaagent::net::HttpMethod ToCoreHttpMethod(const EHttpServerRequestVerbs Verbs)
{
	if (EnumHasAnyFlags(Verbs, EHttpServerRequestVerbs::VERB_POST))
	{
		return metaagent::net::HttpMethod::Post;
	}
	if (EnumHasAnyFlags(Verbs, EHttpServerRequestVerbs::VERB_GET))
	{
		return metaagent::net::HttpMethod::Get;
	}
	return metaagent::net::HttpMethod::Unknown;
}

FString CoreStringToFString(const metaagent::core::String& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}

std::string FStringToCoreString(const FString& Value)
{
	const FTCHARToUTF8 Converter(*Value);
	return std::string(Converter.Get(), static_cast<size_t>(Converter.Length()));
}

void AddBoundRoute(
	const TSharedPtr<IHttpRouter>& Router,
	TArray<FHttpRouteHandle>& Handles,
	const TCHAR* Path,
	const EHttpServerRequestVerbs Verbs,
	const FHttpRequestHandler& Handler)
{
	if (!Router.IsValid())
	{
		return;
	}

	const FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(Path), Verbs, Handler);
	if (Handle.IsValid())
	{
		Handles.Add(Handle);
	}
}

metaagent::net::HttpRequest ToCoreRequest(const FHttpServerRequest& Request)
{
	metaagent::net::HttpRequest CoreRequest;
	CoreRequest.method = ToCoreHttpMethod(Request.Verb);
	CoreRequest.path = FStringToCoreString(Request.RelativePath.GetPath());

	for (const TPair<FString, FString>& Pair : Request.QueryParams)
	{
		if (!CoreRequest.query_string.empty())
		{
			CoreRequest.query_string += "&";
		}
		CoreRequest.query_string += FStringToCoreString(Pair.Key + TEXT("=") + Pair.Value);
	}

	if (Request.Body.Num() > 0)
	{
		CoreRequest.body.assign(
			reinterpret_cast<const char*>(Request.Body.GetData()),
			static_cast<size_t>(Request.Body.Num()));
	}

	return CoreRequest;
}

EHttpServerResponseCodes ToUeStatusCode(const metaagent::net::HttpStatus Status)
{
	switch (Status)
	{
	case metaagent::net::HttpStatus::Ok:
		return EHttpServerResponseCodes::Ok;
	case metaagent::net::HttpStatus::BadRequest:
		return EHttpServerResponseCodes::BadRequest;
	case metaagent::net::HttpStatus::NotFound:
		return EHttpServerResponseCodes::NotFound;
	default:
		return EHttpServerResponseCodes::ServerError;
	}
}
}

FMetaAgentHttpBridge& FMetaAgentHttpBridge::Get()
{
	static FMetaAgentHttpBridge Instance;
	return Instance;
}

FString FMetaAgentHttpBridge::GetStatusText(const FMetaAgentHostSessionSnapshot& SessionSnapshot) const
{
	return CoreStringToFString(metaagent::session::build_http_server_status_text(SessionSnapshot.ToCoreSession()));
}

bool FMetaAgentHttpBridge::Start(const int32 Port, const bool bEnabled, const FCallbacks& Callbacks)
{
	Stop();
	ActiveCallbacks = Callbacks;

	if (!bEnabled)
	{
		UE_LOG(LogMetaAgent, Log, TEXT("HTTP server disabled by config."));
		return false;
	}

	if (!FHttpServerModule::IsAvailable())
	{
		FModuleManager::LoadModuleChecked<FHttpServerModule>(TEXT("HTTPServer"));
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	LocalHttpRouter = HttpServerModule.GetHttpRouter(static_cast<uint32>(Port), true);
	if (!LocalHttpRouter.IsValid())
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("HTTP server failed to bind port %d."), Port);
		return false;
	}

	RouteHandles.Reset();

	const FHttpRequestHandler RouteHandler =
		FHttpRequestHandler::CreateRaw(this, &FMetaAgentHttpBridge::HandleHttpRequest);

	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health"), EHttpServerRequestVerbs::VERB_GET, RouteHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health/"), EHttpServerRequestVerbs::VERB_GET, RouteHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, RouteHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo/"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, RouteHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify"), EHttpServerRequestVerbs::VERB_POST, RouteHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify/"), EHttpServerRequestVerbs::VERB_POST, RouteHandler);

	HttpServerModule.StartAllListeners();
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server listening on port %d. Endpoints: /health, /echo, /notify"), Port);
	return true;
}

void FMetaAgentHttpBridge::Stop()
{
	if (!LocalHttpRouter.IsValid())
	{
		return;
	}

	for (FHttpRouteHandle& RouteHandle : RouteHandles)
	{
		if (RouteHandle.IsValid())
		{
			LocalHttpRouter->UnbindRoute(RouteHandle);
			RouteHandle.Reset();
		}
	}
	RouteHandles.Reset();
	LocalHttpRouter.Reset();
	ActiveCallbacks = FCallbacks();
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server routes unbound."));
}

bool FMetaAgentHttpBridge::HandleHttpRequest(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	const metaagent::net::HttpRequest CoreRequest = ToCoreRequest(Request);
	metaagent::net::HandlerContext Context;
	Context.session = ActiveCallbacks.BuildSessionSnapshot
		? ActiveCallbacks.BuildSessionSnapshot().ToCoreSession()
		: metaagent::session::RuntimeSession {};

	const metaagent::net::RouteTable Routes;
	const metaagent::net::RouteDispatchResult DispatchResult = Routes.dispatch(CoreRequest, Context);
	if (!DispatchResult.handled)
	{
		TUniquePtr<FHttpServerResponse> NotFound = FHttpServerResponse::Create(TEXT("{\"error\":\"not_found\"}"), TEXT("application/json"));
		NotFound->Code = EHttpServerResponseCodes::NotFound;
		OnComplete(MoveTemp(NotFound));
		return true;
	}

	if (DispatchResult.notify.has_notify_message && ActiveCallbacks.OnNotifyMessage)
	{
		ActiveCallbacks.OnNotifyMessage(CoreStringToFString(DispatchResult.notify.notify_message.text));
	}

	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(
		CoreStringToFString(DispatchResult.response.body),
		UTF8_TO_TCHAR(DispatchResult.response.content_type.c_str()));
	HttpResponse->Code = ToUeStatusCode(DispatchResult.response.status);
	OnComplete(MoveTemp(HttpResponse));
	return true;
}
