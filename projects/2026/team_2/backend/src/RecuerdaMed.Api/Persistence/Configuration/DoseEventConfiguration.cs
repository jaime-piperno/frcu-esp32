using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Persistence.Configuration;

public class DoseEventConfiguration : IEntityTypeConfiguration<DoseEvent>
{
    public void Configure(EntityTypeBuilder<DoseEvent> builder)
    {
        builder.ToTable("dose_events");
        builder.HasKey(e => e.Id);

        builder.Property(e => e.ScheduledUtc).HasColumnType("timestamp with time zone");
        builder.Property(e => e.OccurredAtUtc).HasColumnType("timestamp with time zone");
        builder.Property(e => e.Type).HasConversion<int>();
        builder.Property(e => e.Note).HasMaxLength(500);

        builder.HasOne(e => e.Device)
            .WithMany()
            .HasForeignKey(e => e.DeviceId)
            .OnDelete(DeleteBehavior.Cascade);

        builder.HasOne(e => e.Medication)
            .WithMany()
            .HasForeignKey(e => e.MedicationId)
            .OnDelete(DeleteBehavior.Restrict);

        // Idempotency guard for auto-registered / single-shot events (Taken, Missed, AlertSent).
        // Snoozed events are intentionally EXCLUDED: a slot may legitimately have up to
        // MaxSnoozes Snoozed rows, so the uniqueness constraint is partial (filtered index).
        builder.HasIndex(e => new { e.DeviceId, e.MedicationId, e.ScheduledUtc, e.Type })
            .IsUnique()
            .HasFilter("\"Type\" <> 1");

        builder.HasIndex(e => new { e.DeviceId, e.MedicationId, e.ScheduledUtc });
        builder.HasIndex(e => new { e.OccurredAtUtc, e.DeviceId });
    }
}