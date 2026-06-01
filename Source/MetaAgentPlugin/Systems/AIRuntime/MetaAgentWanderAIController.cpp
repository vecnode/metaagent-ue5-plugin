// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Core/MetaAgent.h"
#include "Gameplay/AI/Tasks/MetaAgentBTTask_MoveToPatrolPoint.h"
#include "Gameplay/AI/Tasks/MetaAgentBTTask_SetRandomPatrolPoint.h"

namespace
{
	const FName PatrolLocationKeyName(TEXT("PatrolLocation"));
}

AMetaAgentWanderAIController::AMetaAgentWanderAIController()
{
	bStartAILogicOnPossess = true;
}

void AMetaAgentWanderAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	BuildRuntimeBehaviorTree();
	StartRuntimeBehaviorTree();
}

void AMetaAgentWanderAIController::OnUnPossess()
{
	StopMovement();
	Super::OnUnPossess();
}

void AMetaAgentWanderAIController::BuildRuntimeBehaviorTree()
{
	if (!RuntimeBlackboard)
	{
		RuntimeBlackboard = NewObject<UBlackboardData>(this, TEXT("BB_RuntimeWander"));
		RuntimeBlackboard->UpdatePersistentKey<UBlackboardKeyType_Vector>(PatrolLocationKeyName);
		RuntimeBlackboard->UpdateKeyIDs();
	}

	if (RuntimeBehaviorTree)
	{
		return;
	}

	RuntimeBehaviorTree = NewObject<UBehaviorTree>(this, TEXT("BT_RuntimeWander"));
	RuntimeBehaviorTree->BlackboardAsset = RuntimeBlackboard;

	UBTComposite_Selector* RootSelector = NewObject<UBTComposite_Selector>(RuntimeBehaviorTree, TEXT("RootSelector"));
	RuntimeBehaviorTree->RootNode = RootSelector;

	UBTComposite_Sequence* PatrolSequence = NewObject<UBTComposite_Sequence>(RootSelector, TEXT("PatrolSequence"));
	FBTCompositeChild& PatrolBranch = RootSelector->Children.AddDefaulted_GetRef();
	PatrolBranch.ChildComposite = PatrolSequence;

	UMetaAgentBTTask_SetRandomPatrolPoint* RandomPointTask = NewObject<UMetaAgentBTTask_SetRandomPatrolPoint>(PatrolSequence, TEXT("Task_SetRandomPoint"));
	RandomPointTask->SearchRadius = PatrolRadius;

	UMetaAgentBTTask_MoveToPatrolPoint* MoveToTask = NewObject<UMetaAgentBTTask_MoveToPatrolPoint>(PatrolSequence, TEXT("Task_MoveToPoint"));

	UBTTask_Wait* WaitTask = NewObject<UBTTask_Wait>(PatrolSequence, TEXT("Task_Wait"));
	const float ClampedWaitMin = FMath::Max(0.1f, WaitMinSeconds);
	const float ClampedWaitMax = FMath::Max(ClampedWaitMin, WaitMaxSeconds);
	WaitTask->WaitTime = FValueOrBBKey_Float(ClampedWaitMin);
	WaitTask->RandomDeviation = FValueOrBBKey_Float(ClampedWaitMax - ClampedWaitMin);

	FBTCompositeChild& ChildSetRandom = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildSetRandom.ChildTask = RandomPointTask;

	FBTCompositeChild& ChildMoveTo = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildMoveTo.ChildTask = MoveToTask;

	FBTCompositeChild& ChildWait = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildWait.ChildTask = WaitTask;

	UBTTask_Wait* FallbackWaitTask = NewObject<UBTTask_Wait>(RootSelector, TEXT("Task_FallbackWait"));
	FallbackWaitTask->WaitTime = FValueOrBBKey_Float(1.0f);
	FallbackWaitTask->RandomDeviation = FValueOrBBKey_Float(0.5f);

	FBTCompositeChild& FallbackBranch = RootSelector->Children.AddDefaulted_GetRef();
	FallbackBranch.ChildTask = FallbackWaitTask;
}

void AMetaAgentWanderAIController::StartRuntimeBehaviorTree()
{
	if (!RuntimeBlackboard || !RuntimeBehaviorTree)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: runtime tree initialization failed (missing assets)."));
		return;
	}

	UBlackboardComponent* BlackboardComp = nullptr;
	if (!UseBlackboard(RuntimeBlackboard, BlackboardComp) || !BlackboardComp)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: failed to initialize blackboard."));
		return;
	}

	if (APawn* ControlledPawn = GetPawn())
	{
		BlackboardComp->SetValueAsVector(PatrolLocationKeyName, ControlledPawn->GetActorLocation());
	}

	if (!RunBehaviorTree(RuntimeBehaviorTree))
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: failed to run runtime behavior tree."));
		return;
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("WanderAI: runtime behavior tree started (radius=%.1f wait=[%.1f, %.1f])."),
		PatrolRadius,
		WaitMinSeconds,
		WaitMaxSeconds);
}

