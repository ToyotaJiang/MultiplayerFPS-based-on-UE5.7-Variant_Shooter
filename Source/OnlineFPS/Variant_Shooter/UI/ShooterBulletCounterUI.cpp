// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"

void UShooterBulletCounterUI::NativeConstruct()
{
	Super::NativeConstruct();

	// If KDStatsText is not bound in blueprint, create it in pure C++
	if (!KDStatsText)
	{
		UE_LOG(LogTemp, Warning, TEXT("KDStatsText not bound - creating pure C++ K/D display"));

		// Create TextBlock in C++
		CPPKDStatsText = NewObject<UTextBlock>(this, UTextBlock::StaticClass());
		if (CPPKDStatsText)
		{
			// Set text properties
			CPPKDStatsText->SetText(FText::FromString(TEXT("K/D: 0/0 (0.00)")));
			
			// Set font size and color
			FSlateFontInfo FontInfo = CPPKDStatsText->GetFont();
			FontInfo.Size = 22;
			CPPKDStatsText->SetFont(FontInfo);
			CPPKDStatsText->SetColorAndOpacity(FLinearColor::Yellow);
			
			// Set shadow for better visibility
			FSlateFontInfo FontWithShadow = FontInfo;
			CPPKDStatsText->SetShadowOffset(FVector2D(2.0f, 2.0f));
			CPPKDStatsText->SetShadowColorAndOpacity(FLinearColor::Black);

			// Try to add to canvas panel if available
			if (CanvasPanel)
			{
				UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(CPPKDStatsText);
				if (CanvasSlot)
				{
					// Position: top-left area (below health bar typically)
					CanvasSlot->SetPosition(FVector2D(20.0f, 80.0f));
					CanvasSlot->SetSize(FVector2D(300.0f, 40.0f));
					CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
					CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
					
					UE_LOG(LogTemp, Warning, TEXT("Pure C++ K/D text created and added to canvas at (20, 80)"));
				}
			}
			else
			{
				// If no canvas panel, try to add directly to this widget
				UPanelWidget* Panel = Cast<UPanelWidget>(GetRootWidget());
				if (Panel)
				{
					Panel->AddChild(CPPKDStatsText);
					UE_LOG(LogTemp, Warning, TEXT("Pure C++ K/D text created and added to root panel"));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("Cannot add K/D text - no CanvasPanel found and root is not a panel"));
				}
			}
		}
	}
	else
	{
		// KDStatsText is bound from blueprint
		KDStatsText->SetText(FText::FromString(TEXT("K/D: 0/0 (0.00)")));
		UE_LOG(LogTemp, Warning, TEXT("Using blueprint-bound KDStatsText"));
	}
}

void UShooterBulletCounterUI::UpdateKDStats(const TArray<FPlayerStats>& PlayerStats)
{
	// Use whichever TextBlock is available
	UTextBlock* TextToUpdate = KDStatsText ? KDStatsText : CPPKDStatsText;

	// If neither is available, skip
	if (!TextToUpdate)
	{
		UE_LOG(LogTemp, Error, TEXT("UpdateKDStats: No TextBlock available!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("UpdateKDStats called - PlayerStats count: %d"), PlayerStats.Num());

	// Get local player controller to find our stats
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			// Find our player's stats in the array
			FString PlayerName = PC->GetPlayerState<APlayerState>() ? 
				PC->GetPlayerState<APlayerState>()->GetPlayerName() : TEXT("");

			UE_LOG(LogTemp, Warning, TEXT("  Looking for player: %s"), *PlayerName);

			int32 Kills = 0;
			int32 Deaths = 0;

			for (const FPlayerStats& Stats : PlayerStats)
			{
				UE_LOG(LogTemp, Warning, TEXT("    Checking stats: %s - K:%d D:%d"), 
					*Stats.PlayerName, Stats.Kills, Stats.Deaths);

				if (Stats.PlayerName == PlayerName)
				{
					Kills = Stats.Kills;
					Deaths = Stats.Deaths;
					UE_LOG(LogTemp, Warning, TEXT("  Found matching stats! K:%d D:%d"), Kills, Deaths);
					break;
				}
			}

			// Calculate K/D ratio
			float KDRatio = Deaths > 0 ? (float)Kills / (float)Deaths : (float)Kills;

			// Format the text: "K/D: 5/3 (1.67)"
			FString KDText = FString::Printf(TEXT("K/D: %d/%d (%.2f)"), Kills, Deaths, KDRatio);
			TextToUpdate->SetText(FText::FromString(KDText));

			UE_LOG(LogTemp, Warning, TEXT("  Updated K/D text to: %s"), *KDText);

			// Optional: Change color based on performance
			if (KDRatio >= 2.0f)
			{
				TextToUpdate->SetColorAndOpacity(FLinearColor::Green);
			}
			else if (KDRatio >= 1.0f)
			{
				TextToUpdate->SetColorAndOpacity(FLinearColor::Yellow);
			}
			else
			{
				TextToUpdate->SetColorAndOpacity(FLinearColor::Red);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UpdateKDStats: No PlayerController found!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UpdateKDStats: No World found!"));
	}
}

