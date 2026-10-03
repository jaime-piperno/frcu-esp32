using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Persistence.Configuration;

public class DeviceConfiguration : IEntityTypeConfiguration<Device>
{
    public const string DefaultTimeZoneId = "America/Argentina/Buenos_Aires";

    public void Configure(EntityTypeBuilder<Device> builder)
    {
        builder.ToTable("devices");
        builder.HasKey(d => d.Id);

        builder.Property(d => d.Name).HasMaxLength(100).IsRequired();
        builder.Property(d => d.ApiKey).HasMaxLength(64).IsRequired();
        builder.Property(d => d.TimeZoneId).HasMaxLength(64).IsRequired()
            .HasDefaultValue(DefaultTimeZoneId);
        builder.Property(d => d.CreatedAtUtc).HasColumnType("timestamp with time zone");

        builder.HasIndex(d => d.ApiKey).IsUnique();
    }
}