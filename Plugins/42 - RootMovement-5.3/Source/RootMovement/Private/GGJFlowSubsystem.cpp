#include "GGJFlowSubsystem.h"

#include "Components/ActorComponent.h"
#include "Components/SphereComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookActor.h"
#include "PaperFlipbookComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void UGGJFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	HighestChapter = 0;
	ChapterCardTime = -1.0f;
	WallJumpGuardTime = 0.0f;
	BossHealth = BossMaxHealth;
	BossAttackTime = 0.0f;
	BossFightTime = 0.0f;
	bBossDefeated = false;
	bGameCompleted = false;
	bInputModeConfigured = false;
	bHasTrackedPawn = false;
	TrackedPawn.Reset();
	LastPawnLocation = FVector::ZeroVector;
}

void UGGJFlowSubsystem::Deinitialize()
{
	RemoveOverlay();
	RemoveBossWidget();
	Super::Deinitialize();
}

bool UGGJFlowSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UGGJFlowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGGJFlowSubsystem, STATGROUP_Tickables);
}

int32 UGGJFlowSubsystem::DetermineChapter(const FVector& Location) const
{
	// The level is one continuous map. These gates follow the authored route instead of
	// treating every low point as a later chapter.
	if (Location.X >= 16500.0 && Location.Z <= -16000.0)
	{
		return 4;
	}
	if (Location.Z <= -9000.0)
	{
		return 3;
	}
	// The first pipe/checkpoint transition is around X=4060. Trigger just after it,
	// before the player reaches the cliff and vertical descent.
	if (Location.X >= 4000.0 && Location.Z > -5000.0)
	{
		return 2;
	}
	return 1;
}

void UGGJFlowSubsystem::ShowChapterCard(int32 Chapter)
{
	ShowCardMessage(FText::FromString(FString::Printf(TEXT("\u7b2c%d\u5173"), Chapter)));
}

void UGGJFlowSubsystem::ShowCardMessage(const FText& Message)
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		return;
	}

	if (!OverlayWidget.IsValid())
	{
		TSharedRef<SOverlay> NewOverlay = SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(Backdrop, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ChapterText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 64))
				.ColorAndOpacity(FLinearColor::White)
				.Justification(ETextJustify::Center)
			]
		];
		OverlayWidget = NewOverlay;
		Viewport->AddViewportWidgetContent(NewOverlay, 10000);
	}

	ChapterText->SetText(Message);
	Backdrop->SetVisibility(EVisibility::Visible);
	ChapterCardTime = 0.0f;
	UE_LOG(LogTemp, Display, TEXT("GGJFlow: showing card %s"), *Message.ToString());
}

void UGGJFlowSubsystem::SetupFinalBoss()
{
	if (BossActor.IsValid() || bBossDefeated || !GetWorld())
	{
		return;
	}

	UPaperFlipbook* BossFlipbook = LoadObject<UPaperFlipbook>(nullptr,
		TEXT("/Game/Paperassets/Environment/BACK2/gGGG.gGGG"));
	if (!BossFlipbook)
	{
		UE_LOG(LogTemp, Error, TEXT("GGJFlow: unable to load purple boss flipbook"));
		return;
	}

	// The marked final arena is the camera-restricted room centered here.
	// Spawn a dedicated visual instead of hijacking the moving copy used upstairs.
	BossBaseLocation = FVector(16895.0f, 0.0f, -16960.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APaperFlipbookActor* SpawnedBoss = GetWorld()->SpawnActor<APaperFlipbookActor>(
		APaperFlipbookActor::StaticClass(), BossBaseLocation, FRotator::ZeroRotator, SpawnParams);
	if (!SpawnedBoss)
	{
		return;
	}
	SpawnedBoss->SetActorScale3D(FVector(2.0f));
	if (UPaperFlipbookComponent* Sprite = SpawnedBoss->GetRenderComponent())
	{
		Sprite->SetFlipbook(BossFlipbook);
		Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprite->SetLooping(true);
		Sprite->PlayFromStart();
	}
	BossActor = SpawnedBoss;

	USphereComponent* Hitbox = NewObject<USphereComponent>(BossActor.Get(), TEXT("GGJ_FinalBossHitbox"));
	Hitbox->InitSphereRadius(145.0f);
	Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitbox->SetCollisionObjectType(ECC_WorldDynamic);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Overlap);
	Hitbox->SetupAttachment(BossActor->GetRootComponent());
	BossActor->AddInstanceComponent(Hitbox);
	Hitbox->RegisterComponent();
	Hitbox->OnComponentBeginOverlap.AddDynamic(this, &UGGJFlowSubsystem::OnBossHitboxOverlap);
	BossHitbox = Hitbox;
	UE_LOG(LogTemp, Display, TEXT("GGJFlow: fixed purple boss spawned inside final arena at %s"), *BossBaseLocation.ToString());

	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		TSharedRef<SOverlay> NewBossWidget = SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 42.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.Padding(FMargin(18.0f, 10.0f))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.86f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("FINAL BOSS")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(0.0f, 34.0f, 0.0f, 0.0f))
				[
					SAssignNew(BossHealthBar, SProgressBar)
					.Percent(1.0f)
					.FillColorAndOpacity(FLinearColor(0.8f, 0.05f, 0.08f, 1.0f))
				]
			]
		];
		BossWidget = NewBossWidget;
		Viewport->AddViewportWidgetContent(NewBossWidget, 9000);
	}
}

void UGGJFlowSubsystem::OnBossHitboxOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || bBossDefeated || !OtherActor->GetClass()->GetName().Contains(TEXT("BP_Player_Projectile")))
	{
		return;
	}

	OtherActor->Destroy();
	BossHealth = FMath::Max(0, BossHealth - 1);
	if (BossHealthBar.IsValid())
	{
		BossHealthBar->SetPercent(static_cast<float>(BossHealth) / static_cast<float>(BossMaxHealth));
	}
	if (BossHealth == 0)
	{
		bBossDefeated = true;
		if (BossActor.IsValid())
		{
			BossActor->Destroy();
		}
		BossActor.Reset();
		BossHitbox.Reset();
		RemoveBossWidget();
		ShowCardMessage(FText::FromString(TEXT("BOSS \u5df2\u51fb\u8d25")));
		UE_LOG(LogTemp, Display, TEXT("GGJFlow: purple boss defeated; completion remains at the final mask"));
	}
}

void UGGJFlowSubsystem::SetupCompletionMask()
{
	if (CompletionMaskActor.IsValid() || bGameCompleted || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		const FVector Location = Candidate->GetActorLocation();
		UPaperFlipbookComponent* Sprite = Candidate->FindComponentByClass<UPaperFlipbookComponent>();
		UPaperFlipbook* Flipbook = Sprite ? Sprite->GetFlipbook() : nullptr;
		const FString FlipbookPath = Flipbook ? Flipbook->GetPathName() : FString();
		if (FlipbookPath.Contains(TEXT("/Game/Paperassets/Characters/player1/\u8ffd\u9010\u9b3c\u8138/"))
			&& Location.X > 20500.0f && Location.Z < -16500.0f)
		{
			CompletionMaskActor = Candidate;
			break;
		}
	}
	if (!CompletionMaskActor.IsValid())
	{
		return;
	}

	USphereComponent* Hitbox = NewObject<USphereComponent>(CompletionMaskActor.Get(), TEXT("GGJ_CompletionMaskHitbox"));
	Hitbox->InitSphereRadius(220.0f);
	Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitbox->SetCollisionObjectType(ECC_WorldDynamic);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Overlap);
	Hitbox->SetupAttachment(CompletionMaskActor->GetRootComponent());
	CompletionMaskActor->AddInstanceComponent(Hitbox);
	Hitbox->RegisterComponent();
	Hitbox->OnComponentBeginOverlap.AddDynamic(this, &UGGJFlowSubsystem::OnCompletionMaskOverlap);
	CompletionMaskHitbox = Hitbox;
	UE_LOG(LogTemp, Display, TEXT("GGJFlow: completion mask initialized at %s"),
		*CompletionMaskActor->GetActorLocation().ToString());
}

void UGGJFlowSubsystem::OnCompletionMaskOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (bGameCompleted || !OtherActor || !OtherActor->IsA<APawn>())
	{
		return;
	}
	if (!bBossDefeated)
	{
		ShowCardMessage(FText::FromString(TEXT("\u8bf7\u5148\u51fb\u8d25 BOSS")));
		return;
	}
	bGameCompleted = true;
	ShowCardMessage(FText::FromString(TEXT("\u901a\u5173")));
	UE_LOG(LogTemp, Display, TEXT("GGJFlow: player reached the final mask and completed the game"));
}

void UGGJFlowSubsystem::UpdateFinalBoss(float DeltaTime, APawn* Pawn)
{
	if (bBossDefeated || !Pawn)
	{
		return;
	}
	const FVector ArenaCenter(16895.0f, 0.0f, -16960.0f);
	// Only create the encounter after the player reaches the marked fourth-stage room.
	if (!BossActor.IsValid() && (HighestChapter < 4 || FVector::Dist(Pawn->GetActorLocation(), ArenaCenter) > 2200.0f))
	{
		return;
	}
	SetupFinalBoss();
	if (!BossActor.IsValid())
	{
		return;
	}

	// Keep the idle-only boss fixed in the room even if overlaps or projectiles try to move it.
	BossActor->SetActorLocationAndRotation(BossBaseLocation, FRotator::ZeroRotator,
		false, nullptr, ETeleportType::TeleportPhysics);
	const FVector BossLocation = BossBaseLocation;
	const float Distance = FVector::Dist(Pawn->GetActorLocation(), BossLocation);
	if (Distance > 1800.0f)
	{
		return;
	}
	BossAttackTime -= DeltaTime;
	if (BossAttackTime > 0.0f)
	{
		return;
	}
	BossAttackTime = BossHealth <= BossMaxHealth / 2 ? 0.85f : 1.35f;

	UClass* ProjectileClass = StaticLoadClass(AActor::StaticClass(), nullptr,
		TEXT("/Game/BluePrint/Projectiles/Enemy/BP_Enemy_Projectile_Lizard.BP_Enemy_Projectile_Lizard_C"));
	if (!ProjectileClass)
	{
		return;
	}
	FVector VisualOrigin = BossLocation;
	FVector VisualExtent(120.0f, 0.0f, 120.0f);
	if (UPaperFlipbookComponent* Sprite = BossActor->FindComponentByClass<UPaperFlipbookComponent>())
	{
		VisualOrigin = Sprite->Bounds.Origin;
		VisualExtent = Sprite->Bounds.BoxExtent;
	}
	FVector ToPlayer = Pawn->GetActorLocation() - VisualOrigin;
	ToPlayer.Y = 0.0f;
	const FVector ShotDirection = ToPlayer.GetSafeNormal();
	const float VisualRadius = FMath::Max(VisualExtent.X, VisualExtent.Z);
	const FVector SpawnLocation = VisualOrigin + ShotDirection * (VisualRadius + 45.0f);
	const FRotator SpawnRotation = (Pawn->GetActorLocation() - SpawnLocation).Rotation();
	FActorSpawnParameters Params;
	Params.Owner = BossActor.Get();
	GetWorld()->SpawnActor<AActor>(ProjectileClass, SpawnLocation, SpawnRotation, Params);
}

void UGGJFlowSubsystem::RemoveBossWidget()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (Viewport && BossWidget.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(BossWidget.ToSharedRef());
	}
	BossWidget.Reset();
	BossHealthBar.Reset();
}

void UGGJFlowSubsystem::UpdateChapterCard(float DeltaTime)
{
	if (ChapterCardTime < 0.0f || !Backdrop.IsValid())
	{
		return;
	}

	ChapterCardTime += DeltaTime;
	constexpr float FadeInEnd = 0.12f;
	constexpr float HoldEnd = 0.95f;
	constexpr float FadeOutEnd = 1.35f;
	float Alpha = 1.0f;
	if (ChapterCardTime < FadeInEnd)
	{
		Alpha = ChapterCardTime / FadeInEnd;
	}
	else if (ChapterCardTime > HoldEnd)
	{
		Alpha = 1.0f - ((ChapterCardTime - HoldEnd) / (FadeOutEnd - HoldEnd));
	}

	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	Backdrop->SetBorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, Alpha));
	if (ChapterText.IsValid())
	{
		ChapterText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Alpha)));
	}

	if (ChapterCardTime >= FadeOutEnd)
	{
		Backdrop->SetVisibility(EVisibility::Collapsed);
		ChapterCardTime = -1.0f;
	}
}

void UGGJFlowSubsystem::UpdateWallJumpGuard(float DeltaTime, APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character || !Character->GetCharacterMovement())
	{
		return;
	}

	const FVector Velocity = Character->GetVelocity();
	const bool bWallJumpLaunch = Character->GetCharacterMovement()->IsFalling()
		&& Velocity.Z > 1100.0f
		&& LastVerticalVelocity < 900.0f
		&& FMath::Abs(Velocity.X) > 500.0f;
	if (bWallJumpLaunch)
	{
		// The Blueprint evaluates wall contact every frame. Suppress its slide state briefly
		// after launch so tile seams cannot immediately re-enter wall slide and jitter.
		WallJumpGuardTime = 0.32f;
	}
	LastVerticalVelocity = Velocity.Z;

	if (WallJumpGuardTime <= 0.0f)
	{
		return;
	}
	WallJumpGuardTime = FMath::Max(0.0f, WallJumpGuardTime - DeltaTime);
	if (FBoolProperty* SlidingProperty = FindFProperty<FBoolProperty>(Character->GetClass(), TEXT("IsWallSliding")))
	{
		SlidingProperty->SetPropertyValue_InContainer(Character, false);
	}
}

void UGGJFlowSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* Controller = World->GetFirstPlayerController();
	const FString MapName = World->GetMapName();
	if (MapName.Contains(TEXT("menu")))
	{
		if (Controller && !bInputModeConfigured)
		{
			Controller->bShowMouseCursor = true;
			Controller->bEnableClickEvents = true;
			Controller->bEnableMouseOverEvents = true;
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(InputMode);
			bInputModeConfigured = true;
			UE_LOG(LogTemp, Display, TEXT("GGJFlow: menu mouse and UI input enabled"));
		}
		return;
	}

	if (!MapName.Contains(TEXT("NewMap1_Debug")))
	{
		return;
	}
	if (Controller && !bInputModeConfigured)
	{
		Controller->bShowMouseCursor = false;
		Controller->bEnableClickEvents = false;
		Controller->bEnableMouseOverEvents = false;
		Controller->SetInputMode(FInputModeGameOnly());
		bInputModeConfigured = true;
	}
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	const int32 Chapter = DetermineChapter(PawnLocation);
	const bool bPawnReplaced = bHasTrackedPawn && TrackedPawn.Get() != Pawn;
	const bool bLargeTeleport = bHasTrackedPawn
		&& FVector::DistSquared(PawnLocation, LastPawnLocation) > FMath::Square(1200.0f);
	const bool bRespawned = bPawnReplaced || bLargeTeleport;

	if (!bHasTrackedPawn)
	{
		HighestChapter = Chapter;
		ShowChapterCard(HighestChapter);
	}
	else if (bRespawned)
	{
		// Death can reuse the same pawn and teleport it to a checkpoint, or possess a
		// replacement pawn. In both cases the checkpoint position is authoritative.
		HighestChapter = Chapter;
		ShowChapterCard(HighestChapter);
		UE_LOG(LogTemp, Display, TEXT("GGJFlow: respawn/teleport detected; restored chapter %d at %s"),
			HighestChapter, *PawnLocation.ToString());
	}
	else if (Chapter > HighestChapter && ChapterCardTime < 0.0f)
	{
		// Advance only one chapter at a time. This also keeps the sequence correct
		// after a checkpoint load or a large fall that crosses more than one region.
		++HighestChapter;
		ShowChapterCard(HighestChapter);
	}
	TrackedPawn = Pawn;
	LastPawnLocation = PawnLocation;
	bHasTrackedPawn = true;

	UpdateChapterCard(DeltaTime);
	UpdateWallJumpGuard(DeltaTime, Pawn);
	UpdateFinalBoss(DeltaTime, Pawn);
	SetupCompletionMask();
}

void UGGJFlowSubsystem::RemoveOverlay()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (Viewport && OverlayWidget.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(OverlayWidget.ToSharedRef());
	}
	OverlayWidget.Reset();
	Backdrop.Reset();
	ChapterText.Reset();
}
