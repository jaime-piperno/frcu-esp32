namespace RecuerdaMed.Api.Services;

public sealed record StateMessage(
    Guid DeviceId,
    DateTime UtcNow,
    string TimeZoneId,
    IReadOnlyList<AlertInfo> Alerts,
    IReadOnlyList<NextDoseInfo> NextDoses,
    bool Snoozed,
    bool Missed);

public sealed record AlertInfo(Guid MedicationId, string Name, string Dosage);

public sealed record NextDoseInfo(Guid MedicationId, string Name, DateTime ScheduledUtc);