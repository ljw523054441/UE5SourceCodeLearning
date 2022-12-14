// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementEditorWorldInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Elements/Framework/EngineElementsLibrary.h"

#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

bool UActorElementEditorWorldInterface::GetPivotOffset(const FTypedElementHandle& InElementHandle, FVector& OutPivotOffset)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		OutPivotOffset = Actor->GetPivotOffset();
		return true;
	}

	return false;
}

bool UActorElementEditorWorldInterface::SetPivotOffset(const FTypedElementHandle& InElementHandle, const FVector& InPivotOffset)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		Actor->SetPivotOffset(InPivotOffset);
		return true;
	}

	return false;
}

void UActorElementEditorWorldInterface::NotifyMovementStarted(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		GEditor->BroadcastBeginObjectMovement(*Actor);
	}
}

void UActorElementEditorWorldInterface::NotifyMovementOngoing(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		Actor->PostEditMove(false);
	}
}

void UActorElementEditorWorldInterface::NotifyMovementEnded(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		GEditor->BroadcastEndObjectMovement(*Actor);
		Actor->PostEditMove(true);

		Actor->InvalidateLightingCache();
		Actor->UpdateComponentTransforms();
		Actor->MarkPackageDirty();
	}
}

bool UActorElementEditorWorldInterface::CanDeleteElement(const FTypedElementHandle& InElementHandle)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle);
	return Actor && (GUnrealEd->CanDeleteActor(Actor) || Actor->bIsEditorPreviewActor);
}

bool UActorElementEditorWorldInterface::DeleteElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	const TArray<AActor*> ActorsToDelete = ActorElementDataUtil::GetActorsFromHandles(InElementHandles);
	return ActorsToDelete.Num() > 0
		&& GUnrealEd->DeleteActors(ActorsToDelete, InWorld, InSelectionSet, InDeletionOptions.VerifyDeletionCanHappen(), InDeletionOptions.WarnAboutReferences(), InDeletionOptions.WarnAboutSoftReferences());
}

bool UActorElementEditorWorldInterface::CanDuplicateElement(const FTypedElementHandle& InElementHandle)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle);
	return Actor != nullptr; // All actors can be duplicated
}

void UActorElementEditorWorldInterface::DuplicateElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, const FVector& InLocationOffset, TArray<FTypedElementHandle>& OutNewElements)
{
	const TArray<AActor*> ActorsToDuplicate = ActorElementDataUtil::GetActorsFromHandles(InElementHandles);

	if (ActorsToDuplicate.Num() > 0)
	{
		FEditorDelegates::OnDuplicateActorsBegin.Broadcast();

		TArray<AActor*> NewActors;
		ABrush::SetSuppressBSPRegeneration(true);
		GUnrealEd->DuplicateActors(ActorsToDuplicate, NewActors, InWorld->GetCurrentLevel(), InLocationOffset);
		ABrush::SetSuppressBSPRegeneration(false);

		FEditorDelegates::OnDuplicateActorsEnd.Broadcast();

		bool bRebuildBSP = false;
		OutNewElements.Reserve(OutNewElements.Num() + NewActors.Num());
		for (AActor* NewActor : NewActors)
		{
			// Only rebuild if the new actors will change the BSP as this is expensive
			bRebuildBSP |= NewActor->IsA<ABrush>();

			OutNewElements.Add(UEngineElementsLibrary::AcquireEditorActorElementHandle(NewActor));
		}

		if (bRebuildBSP)
		{
			GEditor->RebuildAlteredBSP();
		}
	}
}
