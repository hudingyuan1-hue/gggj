#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GGJFlowSubsystem.generated.h"

class SBorder;
class STextBlock;
class SWidget;
class SProgressBar;
class USphereComponent;

/** Runtime polish for the original GGJ Blueprint project: chapter cards and wall-jump guard. */
UCLASS()
class ROOTMOVEMENT_API UGGJFlowSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	int32 DetermineChapter(const FVector& Location) const;
	void ShowChapterCard(int32 Chapter);
	void ShowCardMessage(const FText& Message);
	void UpdateChapterCard(float DeltaTime);
	void UpdateWallJumpGuard(float DeltaTime, APawn* Pawn);
	void UpdateFinalBoss(float DeltaTime, APawn* Pawn);
	void SetupFinalBoss();
	void SetupCompletionMask();
	void RemoveBossWidget();
	void RemoveOverlay();

	UFUNCTION()
	void OnBossHitboxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnCompletionMaskOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	TSharedPtr<SWidget> OverlayWidget;
	TSharedPtr<SBorder> Backdrop;
	TSharedPtr<STextBlock> ChapterText;
	TSharedPtr<SWidget> BossWidget;
	TSharedPtr<SProgressBar> BossHealthBar;
	TWeakObjectPtr<AActor> BossActor;
	TWeakObjectPtr<USphereComponent> BossHitbox;
	TWeakObjectPtr<AActor> CompletionMaskActor;
	TWeakObjectPtr<USphereComponent> CompletionMaskHitbox;
	TWeakObjectPtr<APawn> TrackedPawn;
	int32 HighestChapter = 0;
	int32 BossHealth = 20;
	int32 BossMaxHealth = 20;
	float ChapterCardTime = -1.0f;
	float LastVerticalVelocity = 0.0f;
	float WallJumpGuardTime = 0.0f;
	float BossAttackTime = 0.0f;
	float BossFightTime = 0.0f;
	FVector BossBaseLocation = FVector::ZeroVector;
	FVector LastPawnLocation = FVector::ZeroVector;
	bool bBossDefeated = false;
	bool bGameCompleted = false;
	bool bInputModeConfigured = false;
	bool bHasTrackedPawn = false;
};
