#include "MusicHUD.h"
#include "MusicZone.h"

void AMusicHUD::BindToZone(AMusicZone* Zone)
{
	if (!Zone) return;

	ClearActiveZone();

	ActiveZone = Zone;

	Zone->OnBeatScored.AddDynamic(this, &AMusicHUD::HandleBeatScored);
	Zone->OnChallengeEnded.AddDynamic(this, &AMusicHUD::HandleChallengeEnded);

	BP_OnZoneBound(Zone);
}

void AMusicHUD::ClearActiveZone(AMusicZone* IfZoneIsThis)
{
	AMusicZone* Zone = ActiveZone.Get();
	if (!Zone) { BP_OnZoneUnbound(); return; }

	if (IfZoneIsThis && IfZoneIsThis != Zone) return;

	UnbindDelegates();
	ActiveZone = nullptr;

	BP_OnZoneUnbound();
}

void AMusicHUD::UnbindDelegates()
{
	AMusicZone* Zone = ActiveZone.Get();
	if (!Zone) return;

	Zone->OnBeatScored.RemoveDynamic(this, &AMusicHUD::HandleBeatScored);
	Zone->OnChallengeEnded.RemoveDynamic(this, &AMusicHUD::HandleChallengeEnded);
}

void AMusicHUD::HandleBeatScored(bool bSuccess, int32 InSuccess, int32 InFail, float InPercent)
{
	BP_OnScoreUpdated(bSuccess, InSuccess, InFail, InPercent);
}

void AMusicHUD::HandleChallengeEnded(bool bAbandoned, float FinalPercent)
{
	BP_OnChallengeEnded(bAbandoned, FinalPercent);
}
