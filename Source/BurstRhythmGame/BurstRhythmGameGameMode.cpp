// Copyright Epic Games, Inc. All Rights Reserved.

#include "BurstRhythmGameGameMode.h"
#include "BurstRhythmGameCharacter.h"
#include "UObject/ConstructorHelpers.h"

ABurstRhythmGameGameMode::ABurstRhythmGameGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
