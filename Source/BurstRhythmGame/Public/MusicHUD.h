#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MusicHUD.generated.h"

class AMusicZone;

UCLASS()
class BURSTRHYTHMGAME_API AMusicHUD : public AHUD
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Rhythm|HUD")
	void BindToZone(AMusicZone* Zone);

	UFUNCTION(BlueprintCallable, Category="Rhythm|HUD")
	void ClearActiveZone(AMusicZone* IfZoneIsThis = nullptr);

	UFUNCTION(BlueprintImplementableEvent, Category="Rhythm|HUD")
	void BP_OnScoreUpdated(bool bSuccess, int32 SuccessCount, int32 FailCount, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category="Rhythm|HUD")
	void BP_OnChallengeEnded(bool bAbandoned, float FinalPercent);

	UFUNCTION(BlueprintImplementableEvent, Category="Rhythm|HUD")
	void BP_OnZoneBound(AMusicZone* Zone);

	UFUNCTION(BlueprintImplementableEvent, Category="Rhythm|HUD")
	void BP_OnZoneUnbound();

private:
	TWeakObjectPtr<AMusicZone> ActiveZone;

	UFUNCTION()
	void HandleBeatScored(bool bSuccess, int32 InSuccess, int32 InFail, float InPercent);

	UFUNCTION()
	void HandleChallengeEnded(bool bAbandoned, float FinalPercent);

	void UnbindDelegates();
};