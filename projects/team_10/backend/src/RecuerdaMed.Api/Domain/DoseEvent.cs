namespace RecuerdaMed.Api.Domain;

public class DoseEvent
{
    public Guid Id { get; set; }
    public Guid DeviceId { get; set; }
    public Guid MedicationId { get; set; }
    public DateTime ScheduledUtc { get; set; }
    public DoseEventType Type { get; set; }
    public DateTime OccurredAtUtc { get; set; }
    public int? SnoozeSeconds { get; set; }
    public string? Note { get; set; }

    public Device Device { get; set; } = null!;
    public Medication Medication { get; set; } = null!;
}