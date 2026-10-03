namespace RecuerdaMed.Api.Domain;

public class Device
{
    public Guid Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public string ApiKey { get; set; } = string.Empty;
    public string TimeZoneId { get; set; } = "America/Argentina/Buenos_Aires";
    public bool IsActive { get; set; } = true;
    public DateTime CreatedAtUtc { get; set; }

    public List<Medication> Medications { get; set; } = [];
}