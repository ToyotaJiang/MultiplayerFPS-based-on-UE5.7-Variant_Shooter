// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterUI.generated.h"

struct FPlayerStats;

/**
 *  Simple scoreboard UI for a first person shooter game
 */
UCLASS(abstract)
class ONLINEFPS_API UShooterUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Allows Blueprint to update score sub-widgets */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update Score"))
	void BP_UpdateScore(uint8 TeamByte, int32 Score);

	/** Allows Blueprint to update KD statistics */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update KD Stats"))
	void BP_UpdateKDStats(const TArray<FPlayerStats>& PlayerStats);
};
