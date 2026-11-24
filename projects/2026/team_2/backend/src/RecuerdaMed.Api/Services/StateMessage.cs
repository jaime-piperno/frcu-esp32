using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Services;

public sealed record StateMessage(
    Guid DeviceId,
    DateTime UtcNow,
    string TimeZoneId,
    IReadOnlyList<AlertInfo> Alerts,
    IReadOnlyList<NextDoseInfo> NextDoses,
    bool Snoozed,
    bool Missed);

public sealed record AlertInfo(Guid MedicationId, Guid ScheduleId, string Name, decimal DosageAmount, DoseUnit DosageUnit, string? Message)
{
    public DateTime ScheduledUtc { get; init; }

    // Keep the text field for existing firmware while the audio path is developed.
    public string Dosage => $"{DosageAmount} {(DosageUnit == DoseUnit.Milligrams ? "mg" : "pastillas")}";
}

public sealed record NextDoseInfo(Guid MedicationId, string Name, DateTime ScheduledUtc);
