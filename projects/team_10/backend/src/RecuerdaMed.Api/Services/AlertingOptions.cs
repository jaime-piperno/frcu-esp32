namespace RecuerdaMed.Api.Services;

public sealed class AlertingOptions
{
    public const string SectionName = "Alerting";

    public int SnoozeDurationMinutes { get; set; } = 5;
    public int MaxSnoozes { get; set; } = 3;
    public int MissedWindowHours { get; set; } = 4;
}