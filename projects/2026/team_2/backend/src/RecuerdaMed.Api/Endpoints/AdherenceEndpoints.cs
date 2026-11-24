using Microsoft.EntityFrameworkCore;
using NodaTime;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

public sealed record AdherenceDayResponse(DateOnly Date, int Taken, int Missed, int Snoozed, int Pending);

public static class AdherenceEndpoints
{
    public static WebApplication MapAdherenceEndpoints(this WebApplication app)
    {
        app.MapGroup("/api").WithTags("Adherence")
            .MapGet("/adherence", GetSummaryAsync);

        return app;
    }

    private static async Task<IResult> GetSummaryAsync(
        DateOnly? from,
        DateOnly? to,
        Guid? deviceId,
        AppDbContext db,
        IClock clock,
        CancellationToken ct)
    {
        var nowUtc = clock.GetCurrentInstant().ToDateTimeUtc();
        var today = DateOnly.FromDateTime(nowUtc);

        var fromDate = from ?? today.AddDays(-6);
        var toDate = to ?? today;

        if (fromDate > toDate)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["from"] = ["'from' must not be after 'to'."]
            });
        }

        var startUtc = fromDate.ToDateTime(TimeOnly.MinValue, DateTimeKind.Utc);
        var endUtc = toDate.ToDateTime(TimeOnly.MinValue, DateTimeKind.Utc).AddDays(1);

        var events = await db.DoseEvents.AsNoTracking()
            .Where(e => e.ScheduledUtc >= startUtc && e.ScheduledUtc < endUtc)
            .Where(e => !deviceId.HasValue || e.DeviceId == deviceId.Value)
            .ToListAsync(ct);

        var days = new Dictionary<DateOnly, AdherenceDayResponse>();
        for (var d = fromDate; d <= toDate; d = d.AddDays(1))
            days[d] = new AdherenceDayResponse(d, 0, 0, 0, 0);

        foreach (var ev in events)
        {
            var day = DateOnly.FromDateTime(ev.ScheduledUtc);
            if (!days.TryGetValue(day, out var entry))
                continue;

            days[day] = entry with
            {
                Taken = entry.Taken + (ev.Type == DoseEventType.Taken ? 1 : 0),
                Missed = entry.Missed + (ev.Type == DoseEventType.Missed ? 1 : 0),
                Snoozed = entry.Snoozed + (ev.Type == DoseEventType.Snoozed ? 1 : 0)
            };
        }

        var medications = await db.Medications.AsNoTracking()
            .Include(m => m.Schedules)
            .Where(m => m.IsActive && (!deviceId.HasValue || m.DeviceId == deviceId.Value))
            .ToListAsync(ct);

        var deviceIds = medications.Select(m => m.DeviceId).Distinct().ToList();
        var timeZones = await db.Devices.AsNoTracking()
            .Where(d => deviceIds.Contains(d.Id))
            .ToDictionaryAsync(d => d.Id, d => d.TimeZoneId, ct);

        foreach (var medication in medications)
        {
            var timeZone = AlertStateService.ResolveTimeZone(
                timeZones.GetValueOrDefault(medication.DeviceId, "America/Argentina/Buenos_Aires"));

            foreach (var schedule in medication.Schedules.Where(s => s.IsActive))
            {
                for (var d = fromDate; d <= toDate; d = d.AddDays(1))
                {
                    if ((schedule.DaysOfWeek & ToDayOfWeekFlag(d.DayOfWeek)) == 0)
                        continue;

                    var slotUtc = timeZone.AtLeniently(
                        AlertStateService.ToLocalDateTime(new LocalDate(d.Year, d.Month, d.Day), schedule.LocalTime))
                        .ToDateTimeUtc();
                    if (slotUtc > nowUtc)
                        continue;

                    var taken = events.Any(e => e.MedicationId == medication.Id
                                                && e.DeviceId == medication.DeviceId
                                                && e.ScheduledUtc == slotUtc
                                                && e.Type == DoseEventType.Taken);
                    var missed = events.Any(e => e.MedicationId == medication.Id
                                                 && e.DeviceId == medication.DeviceId
                                                 && e.ScheduledUtc == slotUtc
                                                 && e.Type == DoseEventType.Missed);

                    if (taken || missed)
                        continue;

                    var slotDay = DateOnly.FromDateTime(slotUtc);
                    if (days.TryGetValue(slotDay, out var entry))
                        days[slotDay] = entry with { Pending = entry.Pending + 1 };
                }
            }
        }

        return Results.Ok(days.Values.OrderBy(d => d.Date));
    }

    private static DayOfWeekFlags ToDayOfWeekFlag(DayOfWeek day) => day switch
    {
        DayOfWeek.Monday => DayOfWeekFlags.Mon,
        DayOfWeek.Tuesday => DayOfWeekFlags.Tue,
        DayOfWeek.Wednesday => DayOfWeekFlags.Wed,
        DayOfWeek.Thursday => DayOfWeekFlags.Thu,
        DayOfWeek.Friday => DayOfWeekFlags.Fri,
        DayOfWeek.Saturday => DayOfWeekFlags.Sat,
        DayOfWeek.Sunday => DayOfWeekFlags.Sun,
        _ => DayOfWeekFlags.None
    };
}