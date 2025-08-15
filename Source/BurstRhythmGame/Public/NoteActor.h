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

	//Each node stores the time of its impact so animations can be handled locally
	UFUNCTION(BlueprintCallable, Category="Note")
	void SetImpactTime(double InImpactTime) { ImpactTime = InImpactTime; }

	//This handles the scale and how long we want the animation to be
	UFUNCTION(BlueprintCallable, Category="Note")
	void SetEmphasisParams(float InHalfWindowSeconds, float InPeakScale)
	{
		EmphasisHalfWindowSeconds = InHalfWindowSeconds;
		PeakScale = InPeakScale;
	}

	//The main mesh of the note actor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Note|Visuals")
	UStaticMeshComponent* NoteMesh;

	//If the player hits the note successfully, This material is applied to show that the action was successful
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Note|Visuals")
	UMaterialInterface* SuccessMaterial = nullptr;

	//If the player does not hit the note successfully, This material is applied to show that the action was unsuccessful
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
