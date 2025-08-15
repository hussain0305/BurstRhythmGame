#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoteActor.generated.h"

UCLASS()
class BURSTRHYTHMGAME_API ANoteActor : public AActor
{
	GENERATED_BODY()

public:
	ANoteActor();

	UFUNCTION(BlueprintCallable, Category="Note")
	void SetImpactTime(double InImpactTime) { ImpactTime = InImpactTime; }

	UFUNCTION(BlueprintCallable, Category="Note")
	void SetEmphasisParams(float InHalfWindowSeconds, float InPeakScale)
	{
		EmphasisHalfWindowSeconds = InHalfWindowSeconds;
		PeakScale = InPeakScale;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Note|Visuals")
	UStaticMeshComponent* NoteMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Note|Visuals")
	UMaterialInterface* SuccessMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Note|Visuals")
	UMaterialInterface* FailureMaterial = nullptr;

	UFUNCTION(BlueprintCallable, Category="Note")
	void ApplyResult(bool bSuccess);
	
	UFUNCTION(BlueprintCallable, Category="Note")
	void UpdateVisuals(double SongNowSeconds);

protected:
	virtual void BeginPlay() override;

private:
	double ImpactTime = 0.0;

	UPROPERTY(EditAnywhere, Category="Note|Emphasis")
	float EmphasisHalfWindowSeconds = 0.25f;

	UPROPERTY(EditAnywhere, Category="Note|Emphasis")
	float PeakScale = 2.0f;

	FVector BaseScale = FVector::OneVector;
};
