using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;

namespace RecuerdaMed.Api.Services;

public sealed class DeviceAuthService(AppDbContext db)
{
    /// <summary>Returns the active device whose ApiKey matches, or null when unauthorized.</summary>
    public async Task<Device?> AuthenticateAsync(string? apiKey, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(apiKey))
            return null;

        return await db.Devices.AsNoTracking()
            .FirstOrDefaultAsync(d => d.ApiKey == apiKey && d.IsActive, ct);
    }

    public static string GenerateApiKey()
        => Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(16));
}