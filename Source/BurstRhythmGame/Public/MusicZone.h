#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "MusicZone.generated.h"

class ANoteActor;
class USoundWaveProcedural;
class UAudioComponent;

USTRUCT()
struct FPrompt
{
    GENERATED_BODY()
    double Time = 0.0;
    float  Strength = 0.0f;
};

USTRUCT()
struct FActiveNote
{
    GENERATED_BODY()
    TWeakObjectPtr<AActor> Actor;
    double  ImpactTime = 0.0;
    FVector StartPos   = FVector::ZeroVector;
    FVector EndPos     = FVector::ZeroVector;
};

USTRUCT()
struct FHitWindow
{
    GENERATED_BODY()
    double ImpactTime = 0.0;
    TWeakObjectPtr<ANoteActor> Note;
    bool bScored = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnBeatScored, bool, bSuccess, int32, SuccessCount, int32, FailCount, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChallengeEnded, bool, bAbandoned, float, FinalPercent);

UCLASS()
class BURSTRHYTHMGAME_API AMusicZone : public AActor
{
    GENERATED_BODY()

public:
    AMusicZone();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    TFuture<void> AnalysisFuture;

    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float KickCenterHz   = 90.0f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float KickQ          = 1.4f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float SnareCenterHz  = 1800.0f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float SnareQ         = 1.0f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float KickThreshold  = 0.01f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float SnareThreshold = 0.005f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float KickMinSpacing = 0.25f;
    UPROPERTY(EditAnywhere, Category="Rhythm|Detect|Drums") float SnareMinSpacing= 0.2f;

    struct FBiquad
    {
        float a0=1, a1=0, a2=0, b1=0, b2=0;
        float z1=0, z2=0;

        void SetBandPass(float sampleRate, float centerHz, float Q)
        {
            const float w0   = 2.0f * PI * centerHz / sampleRate;
            const float cosw = FMath::Cos(w0);
            const float sinw = FMath::Sin(w0);
            const float alpha = sinw / (2.0f * Q);

            const float b0n =  alpha;
            const float b1n = 0.0f;
            const float b2n = -alpha;
            const float a0n = 1.0f + alpha;
            const float a1n = -2.0f * cosw;
            const float a2n = 1.0f - alpha;

            a0 = b0n / a0n;
            a1 = b1n / a0n;
            a2 = b2n / a0n;
            b1 = a1n / a0n;
            b2 = a2n / a0n;
            z1 = z2 = 0.0f;
        }

        FORCEINLINE float Process(float x)
        {
            const float y = a0*x + z1;
            z1 = a1*x + z2 - b1*y;
            z2 = a2*x - b2*y;
            return y;
        }
    };

    FBiquad KickBP, SnareBP;
    bool bFiltersInited = false;

    float  PrevKickEnergy  = 0.0f;
    float  PrevSnareEnergy = 0.0f;
    double LastKickTime    = -1000.0;
    double LastSnareTime   = -1000.0;

    void InitDrumFilters();

    UPROPERTY(EditAnywhere, Category="Rhythm|Scoring")
    FKey HitKey = EKeys::E;

    UPROPERTY(EditAnywhere, Category="Rhythm|Scoring")
    float ScoreHalfWindowSec = 0.25f;

    UPROPERTY(BlueprintReadOnly, Category="Rhythm|Scoring")
    int32 SuccessCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Rhythm|Scoring")
    int32 FailCount = 0;

    UPROPERTY(BlueprintAssignable, Category="Rhythm|Events")
    FOnBeatScored OnBeatScored;

    UPROPERTY(BlueprintAssignable, Category="Rhythm|Events")
    FOnChallengeEnded OnChallengeEnded;
    
private:
    UPROPERTY(VisibleAnywhere, Category="Zone")
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, Category="Zone")
    UBoxComponent* Trigger;

    UPROPERTY(EditAnywhere, Category="Rhythm|Visual")
    TSubclassOf<ANoteActor> NoteClass;

    UPROPERTY(EditAnywhere, Category="Rhythm|Visual", meta=(ToolTip="Start of lane in LOCAL space (relative to actor)."))
    FVector LaneStartLocal = FVector(0, 0, 400);

    UPROPERTY(EditAnywhere, Category="Rhythm|Visual", meta=(ToolTip="End of lane in LOCAL space (relative to actor)."))
    FVector LaneEndLocal   = FVector(0, 0,   0);

    UPROPERTY(EditAnywhere, Category="Rhythm|Visual")
    float TravelTime = 5.0f;

    UPROPERTY(EditAnywhere, Category="Rhythm|Sync")
    float SyncOffsetSec = 0.08f;

    TArray<float> FullPCM;
    int32  SampleRate   = 0;
    double SongDuration = 0.0;
    double SongStartTime= 0.0;
    bool   bSongStarted = false;

    UPROPERTY()
    USoundWaveProcedural* ProcWave = nullptr;

    UPROPERTY()
    UAudioComponent* AudioComp = nullptr;

    TQueue<FPrompt> PromptBuffer; 
    TArray<FActiveNote> ActiveNotes;

    FThreadSafeBool bAnalyzing    = false;
    FThreadSafeBool bSongFinished = false;

    int32  SamplesPerHop         = 2048;
    double HopDuration           = 0.0;
    int32  AnalysisCursorSamples = 0;

    float  PrevEnergy        = 0.0f;
    double MinOnsetSpacing   = 0.18;
    double LastOnsetTime     = -1000.0;
    float  FluxThreshold     = 0.03f;

    void AskForFile();
    void LoadAndDecodeAudio(const FString& FilePath);
    bool DecodeWav(const FString& FilePath);
    bool DecodeMp3(const FString& FilePath);

    void StartSong();
    USoundWaveProcedural* CreateProceduralFromPCM(const TArray<float>& PCM, int32 InSampleRate);

    void PreRollAnalysis();
    void StartContinuousAnalysis();
    void AnalysisWorker();
    void ProcessFrameForOnsets(const float* Frame, int32 NumSamples, double FrameStartTime);

    void DrainAndSpawn(double Now);
    void UpdateActiveNotes(double Now);

    void StartZoneSession();
    void StopAndReset();

    UFUNCTION()
    void OnTriggerBegin(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp,
                        int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

    UFUNCTION()
    void OnTriggerEnd(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);

    FTimerHandle FileAskDelayHandle;
    FTimerHandle SongEndHandle;

    TArray<FHitWindow> Upcoming;

    void BindInput();
    void OnHitKeyPressed();

    void PushUpcoming(AActor* NoteActor, double ImpactTime);
    void PopFrontUpcoming();
    void TickScoring(double Now);

    class AMusicHUD* GetPlayerMusicHUD() const;

    void BindSelfToHUD();
    void UnbindSelfFromHUD();
};
