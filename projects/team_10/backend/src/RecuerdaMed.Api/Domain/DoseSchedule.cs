namespace RecuerdaMed.Api.Domain;

public class DoseSchedule
{
    public Guid Id { get; set; }
    public Guid MedicationId { get; set; }
    public TimeOnly LocalTime { get; set; }
    public DayOfWeekFlags DaysOfWeek { get; set; } = DayOfWeekFlags.None;
    public bool IsActive { get; set; } = true;

    public Medication Medication { get; set; } = null!;
}