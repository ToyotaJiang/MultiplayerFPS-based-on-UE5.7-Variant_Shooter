// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterBulletCounterUI.generated.h"

struct FPlayerStats;
class UTextBlock;
class UCanvasPanel;

/**
 *  Simple bullet counter UI widget for a first person shooter game
 */
UCLASS(abstract)
class ONLINEFPS_API UShooterBulletCounterUI : public UUserWidget
{
	GENERATED_BODY()
	
public:

	/** Allows Blueprint to update sub-widgets with the new bullet count */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "UpdateBulletCounter"))
	void BP_UpdateBulletCounter(int32 MagazineSize, int32 BulletCount);

	/** Allows Blueprint to update sub-widgets with the new life total and play a damage effect on the HUD */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "Damaged"))
	void BP_Damaged(float LifePercent);

	/** Allows Blueprint to update K/D statistics (displayed near health bar) */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update KD Stats"))
	void BP_UpdateKDStats(const TArray<FPlayerStats>& PlayerStats);

	/** Pure C++ implementation: Update K/D statistics text */
	UFUNCTION(BlueprintCallable, Category = "Shooter")
	void UpdateKDStats(const TArray<FPlayerStats>& PlayerStats);

protected:

	/** Canvas panel to add K/D text to (optional - bind in blueprint if exists) */
	UPROPERTY(meta = (BindWidgetOptional))
	UCanvasPanel* CanvasPanel;

	/** Text block for displaying K/D stats (OPTIONAL - bind this in Widget Blueprint Designer with "Is Variable" checked) */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* KDStatsText;

	/** Pure C++ created K/D text block (created automatically if KDStatsText is not bound) */
	UPROPERTY()
	UTextBlock* CPPKDStatsText;

	virtual void NativeConstruct() override;
};
