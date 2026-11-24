namespace RecuerdaMed.Api.Domain;

public class Medication
{
    public Guid Id { get; set; }
    public Guid DeviceId { get; set; }
    public string Name { get; set; } = string.Empty;
    public decimal DosageAmount { get; set; }
    public DoseUnit DosageUnit { get; set; }
    public bool IsActive { get; set; } = true;

    public Device Device { get; set; } = null!;
    public List<DoseSchedule> Schedules { get; set; } = [];
}
