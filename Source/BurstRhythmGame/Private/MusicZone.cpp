#include "MusicZone.h"

#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "MusicHUD.h"
#include "GameFramework/PlayerController.h"
#include "NoteActor.h"
#include "../ThirdParty/AudioDecoders/dr_wav.h"
#include "../ThirdParty/AudioDecoders/minimp3_ex.h"

AMusicZone::AMusicZone()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
    Trigger->SetupAttachment(Root);
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECollisionResponse::ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);

    Trigger->InitBoxExtent(FVector(200.f, 200.f, 100.f));
}

void AMusicZone::BeginPlay()
{
    Super::BeginPlay();

    if (Trigger)
    {
        Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMusicZone::OnTriggerBegin);
        Trigger->OnComponentEndOverlap  .AddDynamic(this, &AMusicZone::OnTriggerEnd);
    }

    BindInput();
}

void AMusicZone::BindInput()
{
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        EnableInput(PC);
        if (!InputComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MZDBG] No InputComponent after EnableInput"));
            return;
        }
        InputComponent->BindKey(HitKey, IE_Pressed, this, &AMusicZone::OnHitKeyPressed);
    }
}

void AMusicZone::OnTriggerBegin(UPrimitiveComponent* /*OverlappedComp*/, AActor* Other, UPrimitiveComponent* /*OtherComp*/,
                                int32 /*BodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
    if (!Other || Other == this) return;
    if (!Other->IsA(APawn::StaticClass())) return;

    BindSelfToHUD();
    
    if (GetWorld()->GetTimerManager().IsTimerActive(FileAskDelayHandle) || bSongStarted || bAnalyzing)
        return;

    GetWorld()->GetTimerManager().SetTimer(
        FileAskDelayHandle,
        this, &AMusicZone::StartZoneSession,
        1.0f, false
    );
}

void AMusicZone::OnTriggerEnd(UPrimitiveComponent* /*OverlappedComp*/, AActor* Other, UPrimitiveComponent* /*OtherComp*/, int32 /*BodyIndex*/)
{
    if (!Other || Other == this) return;
    if (!Other->IsA(APawn::StaticClass())) return;

    GetWorld()->GetTimerManager().ClearTimer(FileAskDelayHandle);

    
    StopAndReset();

    UnbindSelfFromHUD();
}

void AMusicZone::StartZoneSession()
{
    AskForFile();
}

void AMusicZone::AskForFile()
{
#if WITH_EDITOR
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        UE_LOG(LogTemp, Error, TEXT("[MZDBG] DesktopPlatform module not available"));
        return;
    }

    TArray<FString> OutFiles;
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        /*ParentWindowHandle*/ nullptr,
        TEXT("Choose Audio File"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("Audio Files|*.wav;*.mp3"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bOpened || OutFiles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MZDBG] Dialog canceled or no file selected."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[MZDBG] Selected: %s"), *OutFiles[0]);
    LoadAndDecodeAudio(OutFiles[0]);
#else
    UE_LOG(LogTemp, Warning, TEXT("[MZDBG] WITH_EDITOR is false; dialog disabled in this build."));
#endif
}

void AMusicZone::LoadAndDecodeAudio(const FString& FilePath)
{
    if (AnalysisFuture.IsValid())
    {
        bAnalyzing = false;
        AnalysisFuture.Wait();
        AnalysisFuture = TFuture<void>();
    }

    bool bOk = false;
    if (FilePath.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase))
        bOk = DecodeWav(FilePath);
    else if (FilePath.EndsWith(TEXT(".mp3"), ESearchCase::IgnoreCase))
        bOk = DecodeMp3(FilePath);
    else
        UE_LOG(LogTemp, Warning, TEXT("[MZDBG] Unsupported extension"));

    if (!bOk)
    {
        UE_LOG(LogTemp, Error, TEXT("[MZDBG] Failed to decode: %s"), *FilePath);
        return;
    }

    HopDuration  = double(SamplesPerHop) / double(SampleRate);
    SongDuration = (double)FullPCM.Num() / (double)SampleRate;

    InitDrumFilters();
    PreRollAnalysis();
    StartContinuousAnalysis();
    StartSong();
}

void AMusicZone::InitDrumFilters()
{
    KickBP.SetBandPass((float)SampleRate, KickCenterHz,  KickQ);
    SnareBP.SetBandPass((float)SampleRate, SnareCenterHz, SnareQ);
    PrevKickEnergy = PrevSnareEnergy = 0.0f;
    LastKickTime   = LastSnareTime   = -1000.0;
    bFiltersInited = true;
}

bool AMusicZone::DecodeWav(const FString& FilePath)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
        return false;

    drwav Wav{};
    if (!drwav_init_memory(&Wav, FileData.GetData(), FileData.Num(), nullptr))
        return false;

    SampleRate = (int32)Wav.sampleRate;

    TArray<float> Interleaved;
    Interleaved.SetNumUninitialized((int64)Wav.totalPCMFrameCount * (int32)Wav.channels);
    drwav_uint64 framesRead = drwav_read_pcm_frames_f32(&Wav, Wav.totalPCMFrameCount, Interleaved.GetData());
    drwav_uninit(&Wav);

    if (Wav.channels > 1)
    {
        const int32 Frames = (int32)framesRead;
        FullPCM.SetNumUninitialized(Frames);
        for (int32 i = 0; i < Frames; ++i)
        {
            double sum = 0.0;
            for (uint32 c = 0; c < Wav.channels; ++c)
                sum += Interleaved[i * Wav.channels + c];
            FullPCM[i] = (float)(sum / (double)Wav.channels);
        }
    }
    else
    {
        FullPCM = MoveTemp(Interleaved);
    }
    return true;
}

bool AMusicZone::DecodeMp3(const FString& FilePath)
{
    mp3dec_ex_t MP3{};
    if (mp3dec_ex_open(&MP3, TCHAR_TO_ANSI(*FilePath), MP3D_SEEK_TO_SAMPLE))
        return false;

    SampleRate         = (int32)MP3.info.hz;
    const int32 Channels     = (int32)MP3.info.channels;
    const int64 TotalSamples = (int64)MP3.samples;

    TArray<mp3d_sample_t> PCM16;
    PCM16.SetNumUninitialized(TotalSamples);
    int64 ReadCount = mp3dec_ex_read(&MP3, PCM16.GetData(), MP3.samples);
    mp3dec_ex_close(&MP3);

    if (ReadCount <= 0) return false;

    TArray<float> Interleaved;
    Interleaved.SetNumUninitialized(ReadCount);
    for (int64 i = 0; i < ReadCount; ++i)
        Interleaved[i] = (float)PCM16[i] / 32768.0f;

    if (Channels > 1)
    {
        const int32 Frames = (int32)(ReadCount / Channels);
        FullPCM.SetNumUninitialized(Frames);
        for (int32 i = 0; i < Frames; ++i)
        {
            double sum = 0.0;
            for (int32 c = 0; c < Channels; ++c)
                sum += Interleaved[i * Channels + c];
            FullPCM[i] = (float)(sum / (double)Channels);
        }
    }
    else
    {
        FullPCM = MoveTemp(Interleaved);
    }
    return true;
}

USoundWaveProcedural* AMusicZone::CreateProceduralFromPCM(const TArray<float>& PCM, int32 InSampleRate)
{
    TArray<int16> PCM16;
    PCM16.SetNumUninitialized(PCM.Num());
    for (int32 i = 0; i < PCM.Num(); ++i)
    {
        float v = FMath::Clamp(PCM[i], -1.0f, 1.0f);
        PCM16[i] = (int16)FMath::RoundToInt(v * 32767.0f);
    }

    USoundWaveProcedural* SW = NewObject<USoundWaveProcedural>(this);
    SW->SetSampleRate(InSampleRate);
    SW->NumChannels = 1;
    SW->Duration    = (float)PCM.Num() / (float)InSampleRate;
    SW->SoundGroup  = ESoundGroup::SOUNDGROUP_Default;
    SW->bLooping    = false;

    SW->QueueAudio(reinterpret_cast<const uint8*>(PCM16.GetData()), PCM16.Num() * sizeof(int16));
    return SW;
}

void AMusicZone::StartSong()
{
    ProcWave = CreateProceduralFromPCM(FullPCM, SampleRate);
    AudioComp = UGameplayStatics::SpawnSound2D(this, ProcWave, 1.0f, 1.0f, 0.0f);
    if (!AudioComp)
    {
        UE_LOG(LogTemp, Error, TEXT("[MZDBG] Failed to spawn audio component"));
        return;
    }

    SongStartTime = FPlatformTime::Seconds();
    bSongStarted  = true;
    bSongFinished = false;

    GetWorld()->GetTimerManager().SetTimer(
        SongEndHandle,
        FTimerDelegate::CreateLambda([this]()
        {
            bSongFinished = true;
            bSongStarted = false;
            bAnalyzing    = false;
            StopAndReset();
        }),
        (float)SongDuration + 1.0f, false
    );
}

void AMusicZone::PreRollAnalysis()
{
    const int32 Total = FullPCM.Num();
    HopDuration = double(SamplesPerHop) / double(SampleRate);

    double TimeSec = 0.0;
    const int32 HopsToFeed = FMath::Min<int32>((int32)(8.0 / HopDuration), Total / SamplesPerHop);

    PrevEnergy = 0.0f;
    LastOnsetTime = -1000.0;

    for (int32 h = 0; h < HopsToFeed; ++h)
    {
        const int32 Offset = h * SamplesPerHop;
        ProcessFrameForOnsets(&FullPCM[Offset], SamplesPerHop, TimeSec);
        TimeSec += HopDuration;
    }

    AnalysisCursorSamples = HopsToFeed * SamplesPerHop;
}

void AMusicZone::StartContinuousAnalysis()
{
    bAnalyzing = true;
    AnalysisFuture = Async(EAsyncExecution::Thread, [this]() { AnalysisWorker(); });
}

void AMusicZone::AnalysisWorker()
{
    double TimeSec = double(AnalysisCursorSamples) / double(SampleRate);
    int32 HopCount = 0;

    while (bAnalyzing)
    {
        const int32 NextEnd = AnalysisCursorSamples + SamplesPerHop;
        if (NextEnd > FullPCM.Num())
            break;

        ProcessFrameForOnsets(&FullPCM[AnalysisCursorSamples], SamplesPerHop, TimeSec);

        AnalysisCursorSamples = NextEnd;
        TimeSec += HopDuration;
        HopCount++;

        if (!bAnalyzing || bSongFinished)
            break;

        FPlatformProcess::Sleep(0.002);
    }
}

void AMusicZone::ProcessFrameForOnsets(const float* Frame, int32 NumSamples, double FrameStartTime)
{
    if (!Frame || NumSamples <= 0) return;
    if (!bFiltersInited) { InitDrumFilters(); }

    double sumKick = 0.0;
    double sumSnare = 0.0;

    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float x  = Frame[i];
        const float yk = KickBP.Process(x);
        const float ys = SnareBP.Process(x);

        sumKick  += (double)yk * (double)yk;
        sumSnare += (double)ys * (double)ys;
    }

    const float Ek = (float)(sumKick  / (double)NumSamples);
    const float Es = (float)(sumSnare / (double)NumSamples);

    const float FluxK = FMath::Max(0.0f, Ek - PrevKickEnergy);
    const float FluxS = FMath::Max(0.0f, Es - PrevSnareEnergy);
    PrevKickEnergy  = Ek;
    PrevSnareEnergy = Es;

    bool bSpawned = false;

    if (FluxK > KickThreshold && (FrameStartTime - LastKickTime) >= KickMinSpacing)
    {
        LastKickTime = FrameStartTime;
        PromptBuffer.Enqueue({ FrameStartTime, FluxK });
        bSpawned = true;
    }

    if (!bSpawned && FluxS > SnareThreshold && (FrameStartTime - LastSnareTime) >= SnareMinSpacing)
    {
        LastSnareTime = FrameStartTime;
        PromptBuffer.Enqueue({ FrameStartTime, FluxS });
    }
}

void AMusicZone::DrainAndSpawn(double Now)
{
    const double Horizon = Now + TravelTime;

    FPrompt P;
    while (PromptBuffer.Peek(P) && P.Time <= Horizon)
    {
        PromptBuffer.Dequeue(P);

        if (!NoteClass) continue;

        const FTransform T = GetActorTransform();
        const FVector StartWorld = T.TransformPosition(LaneStartLocal);
        const FVector EndWorld   = T.TransformPosition(LaneEndLocal);

        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        ANoteActor* Note = GetWorld()->SpawnActor<ANoteActor>(NoteClass, StartWorld, FRotator::ZeroRotator, Params);
        if (!Note) continue;

        Note->SetImpactTime(P.Time);
        Note->SetEmphasisParams(/*HalfWindowSeconds=*/0.25f, /*PeakScale=*/2.0f);

        PushUpcoming(Note, P.Time);
        
        FActiveNote AN;
        AN.Actor      = Note;
        AN.ImpactTime = P.Time;
        AN.StartPos   = StartWorld;
        AN.EndPos     = EndWorld;

        ActiveNotes.Add(AN);
    }
}

void AMusicZone::UpdateActiveNotes(double Now)
{
    for (int32 i = ActiveNotes.Num() - 1; i >= 0; --i)
    {
        FActiveNote& N = ActiveNotes[i];
        if (!N.Actor.IsValid())
        {
            ActiveNotes.RemoveAtSwap(i);
            continue;
        }

        const float Alpha = 1.0f - (float)((N.ImpactTime - Now) / TravelTime);
        const FVector Pos = FMath::Lerp(N.StartPos, N.EndPos, Alpha);
        N.Actor->SetActorLocation(Pos);

        if (ANoteActor* NA = Cast<ANoteActor>(N.Actor.Get()))
        {
            NA->UpdateVisuals(Now);
        }

        if (Now >= (N.ImpactTime + 0.5))
        {
            N.Actor->Destroy();
            ActiveNotes.RemoveAtSwap(i);
        }
    }
}

void AMusicZone::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bSongStarted)
        return;

    const double Now    = FPlatformTime::Seconds() - SongStartTime;
    const double NowAdj = Now + SyncOffsetSec;

    DrainAndSpawn(NowAdj);
    UpdateActiveNotes(NowAdj);

    TickScoring(Now);
}

void AMusicZone::StopAndReset()
{
    const bool bAbandoned = (bSongStarted && !bSongFinished);
    const int32 Total = SuccessCount + FailCount;
    const float FinalPercent = (Total > 0) ? (100.0f * (float)SuccessCount / (float)Total) : 0.0f;
    OnChallengeEnded.Broadcast(bAbandoned, FinalPercent);
    
    GetWorld()->GetTimerManager().ClearTimer(FileAskDelayHandle);
    GetWorld()->GetTimerManager().ClearTimer(SongEndHandle);
    
    if (AudioComp)
    {
        AudioComp->Stop();
        AudioComp = nullptr;
    }
    ProcWave = nullptr;
    
    if (AnalysisFuture.IsValid())
    {
        AnalysisFuture.Wait();
        AnalysisFuture = TFuture<void>();
    }
    
    for (FActiveNote& N : ActiveNotes)
    {
        if (N.Actor.IsValid())
            N.Actor->Destroy();
    }
    ActiveNotes.Empty();
    
    FullPCM.Empty();
    PromptBuffer.Empty();
    
    AnalysisCursorSamples = 0;
    SongDuration  = 0.0;
    SongStartTime = 0.0;
    bAnalyzing    = false;
    bSongStarted  = false;
    bSongFinished = true;
    Upcoming.Empty();
    SuccessCount = 0;
    FailCount = 0;
}

void AMusicZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopAndReset();
    UnbindSelfFromHUD();
    Super::EndPlay(EndPlayReason);
}

void AMusicZone::PushUpcoming(AActor* NoteActor, double ImpactTime)
{
    FHitWindow HW;
    HW.ImpactTime = ImpactTime;
    HW.Note = Cast<ANoteActor>(NoteActor);
    Upcoming.Add(HW);
}

void AMusicZone::TickScoring(double Now)
{
    while (Upcoming.Num() > 0)
    {
        const FHitWindow& Front = Upcoming[0];
        const double WindowEnd = Front.ImpactTime + (double)ScoreHalfWindowSec;

        if (Now > WindowEnd)
        {
            if (!Front.bScored)
            {
                if (Front.Note.IsValid())
                {
                    Front.Note->ApplyResult(false);
                }
                FailCount++;
                const int32 Total = SuccessCount + FailCount;
                const float Percent = (Total > 0) ? (100.0f * (float)SuccessCount / (float)Total) : 0.0f;
                OnBeatScored.Broadcast(/*bSuccess=*/false, SuccessCount, FailCount, Percent);
            }
            PopFrontUpcoming();
        }
        else
        {
            break;
        }
    }
}

void AMusicZone::PopFrontUpcoming()
{
    if (Upcoming.Num() > 0)
    {
        Upcoming.RemoveAt(0);
    }
}

void AMusicZone::OnHitKeyPressed()
{
    if (!bSongStarted)
        return;

    const double Now = FPlatformTime::Seconds() - SongStartTime;

    if (Upcoming.Num() == 0)
    {
        FailCount++;
        const int32 Total = SuccessCount + FailCount;
        const float Percent = (Total > 0) ? (100.0f * (float)SuccessCount / (float)Total) : 0.0f;
        OnBeatScored.Broadcast(false, SuccessCount, FailCount, Percent);
        return;
    }

    FHitWindow& Front = Upcoming[0];
    const double WindowStart = Front.ImpactTime - (double)ScoreHalfWindowSec;
    const double WindowEnd   = Front.ImpactTime + (double)ScoreHalfWindowSec;

    if (Now >= WindowStart && Now <= WindowEnd)
    {
        if (!Front.bScored)
        {
            if (Front.Note.IsValid())
            {
                Front.Note->ApplyResult(true);
            }
            Front.bScored = true;
            SuccessCount++;
            const int32 Total = SuccessCount + FailCount;
            const float Percent = (Total > 0) ? (100.0f * (float)SuccessCount / (float)Total) : 0.0f;

            OnBeatScored.Broadcast(true, SuccessCount, FailCount, Percent);

        }
        PopFrontUpcoming();
    }
    else
    {
        FailCount++;
        const int32 Total = SuccessCount + FailCount;
        const float Percent = (Total > 0) ? (100.0f * (float)SuccessCount / (float)Total) : 0.0f;

        OnBeatScored.Broadcast(false, SuccessCount, FailCount, Percent);
    }
}

AMusicHUD* AMusicZone::GetPlayerMusicHUD() const
{
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        return Cast<AMusicHUD>(PC->GetHUD());
    }
    return nullptr;
}

void AMusicZone::BindSelfToHUD()
{
    if (AMusicHUD* HUD = GetPlayerMusicHUD())
    {
        HUD->BindToZone(this);
    }
}

void AMusicZone::UnbindSelfFromHUD()
{
    if (AMusicHUD* HUD = GetPlayerMusicHUD())
    {
        HUD->ClearActiveZone(this);
    }
}