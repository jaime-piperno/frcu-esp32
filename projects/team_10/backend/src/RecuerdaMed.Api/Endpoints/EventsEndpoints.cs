using Microsoft.EntityFrameworkCore;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;

namespace RecuerdaMed.Api.Endpoints;

public sealed record DoseEventResponse(
    Guid Id,
    Guid DeviceId,
    Guid MedicationId,
    DateTime ScheduledUtc,
    string Type,
    DateTime OccurredAtUtc,
    int? SnoozeSeconds,
    string? Note);

public static class EventsEndpoints
{
    private const int MaxResults = 500;

    public static WebApplication MapEventEndpoints(this WebApplication app)
    {
        app.MapGroup("/api").WithTags("Events")
            .MapGet("/events", ListAsync);

        return app;
    }

    private static async Task<IResult> ListAsync(
        DateTimeOffset? from,
        DateTimeOffset? to,
        Guid? medicationId,
        Guid? deviceId,
        AppDbContext db,
        CancellationToken ct)
    {
        var query = db.DoseEvents.AsNoTracking().AsQueryable();

        if (deviceId.HasValue)
            query = query.Where(e => e.DeviceId == deviceId.Value);
        if (medicationId.HasValue)
            query = query.Where(e => e.MedicationId == medicationId.Value);
        if (from.HasValue)
            query = query.Where(e => e.OccurredAtUtc >= from.Value.UtcDateTime);
        if (to.HasValue)
            query = query.Where(e => e.OccurredAtUtc <= to.Value.UtcDateTime);

        var events = await query
            .OrderByDescending(e => e.OccurredAtUtc)
            .Take(MaxResults)
            .ToListAsync(ct);

        var response = events
            .Select(e => new DoseEventResponse(
                e.Id,
                e.DeviceId,
                e.MedicationId,
                e.ScheduledUtc,
                e.Type.ToString().ToLowerInvariant(),
                e.OccurredAtUtc,
                e.SnoozeSeconds,
                e.Note))
            .ToList();

        return Results.Ok(response);
    }
}