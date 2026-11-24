using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Persistence.Configuration;

public class MedicationConfiguration : IEntityTypeConfiguration<Medication>
{
    public void Configure(EntityTypeBuilder<Medication> builder)
    {
        builder.ToTable("medications");
        builder.HasKey(m => m.Id);

        builder.Property(m => m.Name).HasMaxLength(200).IsRequired();
        builder.Property(m => m.DosageAmount).HasPrecision(10, 3).IsRequired();
        builder.Property(m => m.DosageUnit).HasConversion<short>().IsRequired();
        builder.ToTable(t =>
        {
            t.HasCheckConstraint("CK_medications_DosageAmount_positive", "\"DosageAmount\" > 0");
            t.HasCheckConstraint("CK_medications_DosageUnit_valid", "\"DosageUnit\" IN (0, 1)");
        });

        builder.HasOne(m => m.Device)
            .WithMany(d => d.Medications)
            .HasForeignKey(m => m.DeviceId)
            .OnDelete(DeleteBehavior.Cascade);

        builder.HasIndex(m => m.DeviceId);
    }
}
