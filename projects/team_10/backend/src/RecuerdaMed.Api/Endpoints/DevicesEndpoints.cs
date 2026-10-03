using Microsoft.EntityFrameworkCore;
using NodaTime;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

public sealed record RegisterDeviceRequest(string? Name);

public sealed record UpdateDeviceRequest(string? Name, string? TimeZoneId);

public sealed record DeviceSummaryResponse(
    Guid Id, string Name, string TimeZoneId, bool IsActive, DateTime CreatedAtUtc, int MedicationCount);

public sealed record DeviceDetailResponse(
    Guid Id, string Name, string TimeZoneId, bool IsActive, DateTime CreatedAtUtc,
    IReadOnlyList<MedicationResponse> Medications);

public static class DevicesEndpoints
{
    public static WebApplication MapDeviceEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/devices").WithTags("Devices");

        group.MapPost("/register", RegisterAsync);
        group.MapGet("/", ListAsync);
        group.MapGet("/{id:guid}", GetAsync);
        group.MapPut("/{id:guid}", UpdateAsync);
        group.MapDelete("/{id:guid}", SoftDeleteAsync);
        group.MapGet("/{id:guid}/state", GetStateAsync);
        group.MapPost("/{id:guid}/events", PostEventAsync);

        return app;
    }

    private static async Task<IResult> RegisterAsync(
        RegisterDeviceRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(request.Name))
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["name"] = ["Device name is required."]
            });
        }

        var device = new Device
        {
            Id = Guid.NewGuid(),
            Name = request.Name.Trim(),
            ApiKey = DeviceAuthService.GenerateApiKey(),
            CreatedAtUtc = DateTime.UtcNow
        };

        db.Devices.Add(device);
        await db.SaveChangesAsync(ct);

        // A new active device must be picked up by the publisher without waiting for
        // the idle interval.
        signal.Notify();

        return Results.Ok(new { deviceId = device.Id, apiKey = device.ApiKey });
    }

    private static async Task<IResult> ListAsync(AppDbContext db, CancellationToken ct)
    {
        var devices = await db.Devices.AsNoTracking()
            .Where(d => d.IsActive)
            .OrderBy(d => d.Name)
            .Select(d => new DeviceSummaryResponse(
                d.Id, d.Name, d.TimeZoneId, d.IsActive, d.CreatedAtUtc,
                d.Medications.Count(m => m.IsActive)))
            .ToListAsync(ct);

        return Results.Ok(devices);
    }

    private static async Task<IResult> GetAsync(Guid id, AppDbContext db, CancellationToken ct)
    {
        var device = await db.Devices.AsNoTracking()
            .Where(d => d.Id == id)
            .Select(d => new DeviceDetailResponse(
                d.Id, d.Name, d.TimeZoneId, d.IsActive, d.CreatedAtUtc,
                d.Medications
                    .Where(m => m.IsActive)
                    .OrderBy(m => m.Name)
                    .Select(m => new MedicationResponse(
                        m.Id, m.Name, m.Dosage, m.IsActive,
                        m.Schedules
                            .Where(s => s.IsActive)
                            .OrderBy(s => s.LocalTime)
                            .Select(s => new ScheduleResponse(s.Id, s.LocalTime, s.DaysOfWeek, s.IsActive))
                            .ToList()))
                    .ToList()))
            .FirstOrDefaultAsync(ct);

        return device is null ? Results.NotFound() : Results.Ok(device);
    }

    private static async Task<IResult> UpdateAsync(
        Guid id, UpdateDeviceRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var errors = new Dictionary<string, string[]>();

        if (!string.IsNullOrWhiteSpace(request.Name) && string.IsNullOrWhiteSpace(request.Name.Trim()))
            errors["name"] = ["Device name cannot be empty."];

        if (!string.IsNullOrWhiteSpace(request.TimeZoneId)
            && DateTimeZoneProviders.Tzdb.GetZoneOrNull(request.TimeZoneId) is null)
        {
            errors["timeZoneId"] = [$"Unknown IANA time zone '{request.TimeZoneId}'."];
        }

        if (errors.Count > 0)
            return Results.ValidationProblem(errors);

        var device = await db.Devices.FirstOrDefaultAsync(d => d.Id == id && d.IsActive, ct);
        if (device is null)
            return Results.NotFound();

        if (!string.IsNullOrWhiteSpace(request.Name))
            device.Name = request.Name.Trim();
        if (!string.IsNullOrWhiteSpace(request.TimeZoneId))
            device.TimeZoneId = request.TimeZoneId;

        await db.SaveChangesAsync(ct);

        // Time zone changes affect slot computation: recompute the publisher's wake.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<IResult> SoftDeleteAsync(
        Guid id, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var device = await db.Devices.FirstOrDefaultAsync(d => d.Id == id && d.IsActive, ct);
        if (device is null)
            return Results.NotFound();

        device.IsActive = false;
        await db.SaveChangesAsync(ct);

        // Drop the device from the publisher's active set without waiting.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<IResult> GetStateAsync(
        Guid id, DeviceAuthService auth, AlertStateService alerts, HttpContext http, CancellationToken ct)
    {
        var device = await AuthenticateDeviceAsync(id, auth, http, ct);
        if (device is null)
            return Results.Unauthorized();

        var state = await alerts.UpdateAndGetStateAsync(id, ct);
        return Results.Ok(state);
    }

    private static async Task<IResult> PostEventAsync(
        Guid id,
        DeviceEventRequest request,
        DeviceAuthService auth,
        AlertStateService alerts,
        MqttService mqtt,
        AdherenceNotifier notifier,
        StateChangeSignal signal,
        HttpContext http,
        CancellationToken ct)
    {
        var device = await AuthenticateDeviceAsync(id, auth, http, ct);
        if (device is null)
            return Results.Unauthorized();

        var type = request.ToDoseEventType();
        if (type is null || request.MedicationId == Guid.Empty)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["type"] = ["Type must be 'taken' or 'snoozed'."],
                ["medicationId"] = ["MedicationId is required."]
            });
        }

        var result = await alerts.RecordEventAsync(
            id, type.Value, request.MedicationId, request.OccurredAtUtc, request.SnoozeSeconds, ct: ct);

        await mqtt.PublishStateAsync(id, result.State, ct);
        await notifier.NotifyEventAsync(id, AdherenceNotifier.EventPayload(result.Event), ct);

        // The event changed device state: wake the publisher so it recomputes its
        // next wake-up (e.g. a snooze must re-awaken the publisher at its expiry).
        signal.Notify();

        return Results.Ok(result.State);
    }

    private static async Task<Device?> AuthenticateDeviceAsync(
        Guid deviceId, DeviceAuthService auth, HttpContext http, CancellationToken ct)
    {
        var apiKey = http.Request.Headers["X-Api-Key"].ToString();
        var device = await auth.AuthenticateAsync(apiKey, ct);
        return device is not null && device.Id == deviceId ? device : null;
    }
}