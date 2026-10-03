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
        builder.Property(m => m.Dosage).HasMaxLength(50).IsRequired();

        builder.HasOne(m => m.Device)
            .WithMany(d => d.Medications)
            .HasForeignKey(m => m.DeviceId)
            .OnDelete(DeleteBehavior.Cascade);

        builder.HasIndex(m => m.DeviceId);
    }
}