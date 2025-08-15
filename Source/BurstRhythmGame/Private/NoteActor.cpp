#include "NoteActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"

ANoteActor::ANoteActor()
{
	PrimaryActorTick.bCanEverTick = false;

	NoteMesh = CreateDefaultSubobject<UStaticMeshComponent>(FName("NoteMesh"));
	NoteMesh->SetupAttachment(GetRootComponent());
	NoteMesh->SetWorldScale3D(FVector3d(0.1f, 0.1f, 0.1f));
}

void ANoteActor::BeginPlay()
{
	Super::BeginPlay();
	BaseScale = GetActorScale3D();
}

void ANoteActor::UpdateVisuals(double SongNowSeconds)
{
	const double t0 = ImpactTime - (double)EmphasisHalfWindowSeconds;
	const double t1 = ImpactTime + (double)EmphasisHalfWindowSeconds;

	float S = 1.0f;

	if (SongNowSeconds >= t0 && SongNowSeconds <= t1)
	{
		const double norm = (SongNowSeconds - t0) / (double)(2.0 * EmphasisHalfWindowSeconds);
		const double tri = 1.0 - 2.0 * FMath::Abs(norm - 0.5);
		S = FMath::Lerp(1.0f, PeakScale, (float)tri);
	}

	SetActorScale3D(BaseScale * S);
}

void ANoteActor::ApplyResult(bool bSuccess)
{
	if (!NoteMesh)
		return;

	if (bSuccess && SuccessMaterial)
	{
		NoteMesh->SetMaterial(0, SuccessMaterial);
	}
	else if (!bSuccess && FailureMaterial)
	{
		NoteMesh->SetMaterial(0, FailureMaterial);
	}
}