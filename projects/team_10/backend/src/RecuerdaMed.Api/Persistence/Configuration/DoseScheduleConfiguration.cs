using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Persistence.Configuration;

public class DoseScheduleConfiguration : IEntityTypeConfiguration<DoseSchedule>
{
    public void Configure(EntityTypeBuilder<DoseSchedule> builder)
    {
        builder.ToTable("dose_schedules");
        builder.HasKey(s => s.Id);

        builder.Property(s => s.LocalTime);
        builder.Property(s => s.DaysOfWeek).HasConversion<byte>();

        builder.HasOne(s => s.Medication)
            .WithMany(m => m.Schedules)
            .HasForeignKey(s => s.MedicationId)
            .OnDelete(DeleteBehavior.Cascade);

        builder.HasIndex(s => s.MedicationId);
    }
}