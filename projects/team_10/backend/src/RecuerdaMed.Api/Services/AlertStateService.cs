using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using NodaTime;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;

namespace RecuerdaMed.Api.Services;

public sealed class NotFoundException(string message) : Exception(message);

/// <summary>Result of persisting a device event: the recomputed state and the
/// DoseEvent that was persisted (or the pre-existing one on a duplicate).</summary>
public sealed record RecordEventResult(StateMessage State, DoseEvent Event);

/// <summary>
/// The brain of RecuerdaMed. Computes the per-slot alert state for a device,
/// registers Missed events (idempotently), and persists device events (taken/snoozed).
/// The clock is injected as a NodaTime <see cref="IClock"/> for testability.
/// </summary>
public sealed class AlertStateService
{
    private const int PastDays = 1;
    private const int FutureDays = 6;
    private const string FallbackTimeZoneId = "America/Argentina/Buenos_Aires";

    private readonly AppDbContext _db;
    private readonly AlertingOptions _options;
    private readonly IClock _clock;

    public AlertStateService(AppDbContext db, IOptions<AlertingOptions> options, IClock clock)
    {
        _db = db;
        _options = options.Value;
        _clock = clock;
    }

    public static DateTimeZone ResolveTimeZone(string timeZoneId)
        => DateTimeZoneProviders.Tzdb.GetZoneOrNull(timeZoneId)
           ?? DateTimeZoneProviders.Tzdb[FallbackTimeZoneId];

    public static LocalDateTime ToLocalDateTime(LocalDate date, TimeOnly time)
        => new(date.Year, date.Month, date.Day, time.Hour, time.Minute, time.Second, time.Millisecond);

    /// <summary>
    /// Recomputes the device state and registers any Missed events that became due
    /// (3rd snooze expired, or missed window elapsed). Missed registration is idempotent.
    /// </summary>
    public async Task<StateMessage> UpdateAndGetStateAsync(Guid deviceId, CancellationToken ct = default)
    {
        var device = await _db.Devices.AsNoTracking()
            .FirstOrDefaultAsync(d => d.Id == deviceId && d.IsActive, ct)
            ?? throw new NotFoundException($"Device '{deviceId}' was not found.");

        var timeZone = ResolveTimeZone(device.TimeZoneId);
        var now = _clock.GetCurrentInstant();
        var nowUtc = now.ToDateTimeUtc();

        var medications = await _db.Medications.AsNoTracking()
            .Where(m => m.DeviceId == deviceId && m.IsActive)
            .Include(m => m.Schedules)
            .ToListAsync(ct);

        var slots = BuildSlots(medications, timeZone, now);
        var eventsByKey = await LoadSlotEventsAsync(deviceId, slots, ct);

        var alerts = new List<AlertInfo>();
        var nextDoses = new List<NextDoseInfo>();
        var missedToAdd = new List<DoseEvent>();
        var anySnoozed = false;
        var anyMissed = false;

        foreach (var slot in slots.OrderBy(s => s.ScheduledUtc))
        {
            var slotEvents = eventsByKey.GetValueOrDefault((slot.MedicationId, slot.ScheduledUtc)) ?? [];

            if (slotEvents.Any(e => e.Type == DoseEventType.Taken))
                continue;

            if (nowUtc < slot.ScheduledUtc)
            {
                nextDoses.Add(new NextDoseInfo(slot.MedicationId, slot.MedicationName, slot.ScheduledUtc));
                continue;
            }

            if (slotEvents.Any(e => e.Type == DoseEventType.Missed))
            {
                anyMissed = true;
                continue;
            }

            var snoozes = slotEvents.Where(e => e.Type == DoseEventType.Snoozed)
                .OrderBy(e => e.OccurredAtUtc)
                .ToList();

            var snoozeSeconds = _options.SnoozeDurationMinutes * 60;
            var snoozeActive = false;

            if (snoozes.Count > 0)
            {
                var lastSnooze = snoozes[^1];
                snoozeSeconds = lastSnooze.SnoozeSeconds ?? snoozeSeconds;
                snoozeActive = nowUtc < lastSnooze.OccurredAtUtc.AddSeconds(snoozeSeconds);

                if (!snoozeActive && snoozes.Count >= _options.MaxSnoozes)
                {
                    missedToAdd.Add(NewMissedEvent(deviceId, slot, nowUtc, "Missed after maximum snoozes"));
                    anyMissed = true;
                    continue;
                }
            }

            if (nowUtc >= slot.ScheduledUtc.AddHours(_options.MissedWindowHours))
            {
                missedToAdd.Add(NewMissedEvent(deviceId, slot, nowUtc, "Missed (no response within window)"));
                anyMissed = true;
                continue;
            }

            if (snoozeActive)
                anySnoozed = true;
            else
                alerts.Add(new AlertInfo(slot.MedicationId, slot.MedicationName, slot.MedicationDosage));
        }

        if (missedToAdd.Count > 0)
        {
            _db.DoseEvents.AddRange(missedToAdd);
            try
            {
                await _db.SaveChangesAsync(ct);
            }
            catch (DbUpdateException)
            {
                // Concurrent registration for the same slot: the unique index already
                // rejected the duplicate. Idempotency is preserved.
                _db.ChangeTracker.Clear();
            }
        }

        return new StateMessage(deviceId, nowUtc, device.TimeZoneId, alerts, nextDoses, anySnoozed, anyMissed);
    }

    /// <summary>
    /// Returns the next UTC instant when this device's state will change on its own
    /// (a future dose slot becoming alerting, a snooze expiring, or the missed window
    /// closing), or null when nothing is scheduled in the visible horizon.
    /// Read-only: never registers events, so it is cheap enough to run for every
    /// active device on each publisher wake-up.
    /// </summary>
    public async Task<DateTime?> GetNextStateChangeUtcAsync(Guid deviceId, CancellationToken ct = default)
    {
        var device = await _db.Devices.AsNoTracking()
            .FirstOrDefaultAsync(d => d.Id == deviceId && d.IsActive, ct)
            ?? throw new NotFoundException($"Device '{deviceId}' was not found.");

        var timeZone = ResolveTimeZone(device.TimeZoneId);
        var now = _clock.GetCurrentInstant();
        var nowUtc = now.ToDateTimeUtc();

        var medications = await _db.Medications.AsNoTracking()
            .Where(m => m.DeviceId == deviceId && m.IsActive)
            .Include(m => m.Schedules)
            .ToListAsync(ct);

        var slots = BuildSlots(medications, timeZone, now);
        var eventsByKey = await LoadSlotEventsAsync(deviceId, slots, ct);

        DateTime? next = null;

        foreach (var slot in slots.OrderBy(s => s.ScheduledUtc))
        {
            var slotEvents = eventsByKey.GetValueOrDefault((slot.MedicationId, slot.ScheduledUtc)) ?? [];

            // A slot with a Taken event will never change again.
            if (slotEvents.Any(e => e.Type == DoseEventType.Taken))
                continue;

            DateTime? candidate;

            if (nowUtc < slot.ScheduledUtc)
            {
                // Future slot: it will start alerting at its scheduled time.
                candidate = slot.ScheduledUtc;
            }
            else if (slotEvents.Any(e => e.Type == DoseEventType.Missed))
            {
                // Already missed: terminal state, no future change.
                continue;
            }
            else
            {
                var snoozes = slotEvents.Where(e => e.Type == DoseEventType.Snoozed)
                    .OrderBy(e => e.OccurredAtUtc)
                    .ToList();

                var snoozeSeconds = _options.SnoozeDurationMinutes * 60;
                DateTime? snoozeExpiry = null;

                if (snoozes.Count > 0)
                {
                    var lastSnooze = snoozes[^1];
                    snoozeSeconds = lastSnooze.SnoozeSeconds ?? snoozeSeconds;
                    snoozeExpiry = lastSnooze.OccurredAtUtc.AddSeconds(snoozeSeconds);
                }

                if (snoozeExpiry.HasValue && nowUtc < snoozeExpiry.Value)
                {
                    // Snooze is active: the state flips when the snooze expires
                    // (re-alert, or missed when snoozes are exhausted) — or earlier
                    // when the missed window closes while still snoozed, which
                    // UpdateAndGetStateAsync also honors. Take whichever comes first.
                    candidate = snoozeExpiry;
                    var missedWindowClose = slot.ScheduledUtc.AddHours(_options.MissedWindowHours);
                    if (missedWindowClose > nowUtc && missedWindowClose < candidate.Value)
                        candidate = missedWindowClose;
                }
                else
                {
                    // Alerting now (or snoozes exhausted but under MaxSnoozes): the
                    // only future transition is the missed window closing.
                    candidate = slot.ScheduledUtc.AddHours(_options.MissedWindowHours);
                }
            }

            if (candidate.HasValue && candidate.Value > nowUtc && (!next.HasValue || candidate.Value < next.Value))
                next = candidate;
        }

        return next;
    }

    /// <summary>
    /// Persists a device event (taken/snoozed) for the medication's current slot
    /// (idempotent) and returns the recomputed device state together with the
    /// persisted DoseEvent (or the existing one when the insert was a duplicate).
    /// </summary>
    public async Task<RecordEventResult> RecordEventAsync(
        Guid deviceId,
        DoseEventType type,
        Guid medicationId,
        DateTimeOffset? occurredAtUtc = null,
        int? snoozeSeconds = null,
        string? note = null,
        CancellationToken ct = default)
    {
        var device = await _db.Devices.AsNoTracking()
            .FirstOrDefaultAsync(d => d.Id == deviceId && d.IsActive, ct)
            ?? throw new NotFoundException($"Device '{deviceId}' was not found.");

        var medication = await _db.Medications.AsNoTracking()
            .Include(m => m.Schedules)
            .FirstOrDefaultAsync(m => m.Id == medicationId && m.DeviceId == deviceId && m.IsActive, ct)
            ?? throw new NotFoundException($"Medication '{medicationId}' was not found for device '{deviceId}'.");

        var occurred = DateTime.SpecifyKind(
            occurredAtUtc?.UtcDateTime ?? _clock.GetCurrentInstant().ToDateTimeUtc(),
            DateTimeKind.Utc);

        var scheduledUtc = ResolveScheduledUtc(medication, ResolveTimeZone(device.TimeZoneId), occurred);

        var duplicate = type == DoseEventType.Snoozed
            ? await _db.DoseEvents.AsNoTracking().AnyAsync(
                e => e.DeviceId == deviceId && e.MedicationId == medicationId
                     && e.ScheduledUtc == scheduledUtc && e.Type == DoseEventType.Snoozed
                     && e.OccurredAtUtc == occurred, ct)
            : await _db.DoseEvents.AsNoTracking().AnyAsync(
                e => e.DeviceId == deviceId && e.MedicationId == medicationId
                     && e.ScheduledUtc == scheduledUtc && e.Type == type, ct);

        if (!duplicate)
        {
            var createdEvent = new DoseEvent
            {
                Id = Guid.NewGuid(),
                DeviceId = deviceId,
                MedicationId = medicationId,
                ScheduledUtc = scheduledUtc,
                Type = type,
                OccurredAtUtc = occurred,
                SnoozeSeconds = type == DoseEventType.Snoozed
                    ? (snoozeSeconds ?? _options.SnoozeDurationMinutes * 60)
                    : null,
                Note = note
            };

            _db.DoseEvents.Add(createdEvent);

            try
            {
                await _db.SaveChangesAsync(ct);
                return new RecordEventResult(await UpdateAndGetStateAsync(deviceId, ct), createdEvent);
            }
            catch (DbUpdateException)
            {
                // Duplicate (e.g. MQTT delivery retry): unique index rejected the insert.
                _db.ChangeTracker.Clear();
            }
        }

        // The insert was skipped or rejected as a duplicate: report the existing
        // event so callers still have a full DoseEvent to push to dashboards.
        var existing = await _db.DoseEvents.AsNoTracking().FirstAsync(
            e => e.DeviceId == deviceId && e.MedicationId == medicationId
                 && e.ScheduledUtc == scheduledUtc && e.Type == type, ct);

        return new RecordEventResult(await UpdateAndGetStateAsync(deviceId, ct), existing);
    }

    private async Task<Dictionary<(Guid, DateTime), List<DoseEvent>>> LoadSlotEventsAsync(
        Guid deviceId, IReadOnlyList<Slot> slots, CancellationToken ct)
    {
        if (slots.Count == 0)
            return new Dictionary<(Guid, DateTime), List<DoseEvent>>();

        var medicationIds = slots.Select(s => s.MedicationId).Distinct().ToList();
        var minSlot = slots.Min(s => s.ScheduledUtc);
        var maxSlot = slots.Max(s => s.ScheduledUtc);

        var events = await _db.DoseEvents.AsNoTracking()
            .Where(e => e.DeviceId == deviceId
                        && medicationIds.Contains(e.MedicationId)
                        && e.ScheduledUtc >= minSlot
                        && e.ScheduledUtc <= maxSlot)
            .ToListAsync(ct);

        return events
            .GroupBy(e => (e.MedicationId, e.ScheduledUtc))
            .ToDictionary(g => g.Key, g => g.ToList());
    }

    private static List<Slot> BuildSlots(IReadOnlyList<Medication> medications, DateTimeZone timeZone, Instant now)
    {
        var today = now.InZone(timeZone).Date;
        var slots = new List<Slot>();

        for (var dayOffset = -PastDays; dayOffset <= FutureDays; dayOffset++)
        {
            var localDate = today.PlusDays(dayOffset);
            var flag = ToDayOfWeekFlag(localDate.DayOfWeek);

            foreach (var medication in medications)
            {
                foreach (var schedule in medication.Schedules.Where(s => s.IsActive))
                {
                    if ((schedule.DaysOfWeek & flag) == 0)
                        continue;

                    var scheduledUtc = timeZone.AtLeniently(ToLocalDateTime(localDate, schedule.LocalTime)).ToDateTimeUtc();
                    slots.Add(new Slot(medication.Id, scheduledUtc, medication.Name, medication.Dosage));
                }
            }
        }

        return slots;
    }

    private static DateTime ResolveScheduledUtc(Medication medication, DateTimeZone timeZone, DateTime occurredUtc)
    {
        var localDate = Instant.FromDateTimeUtc(occurredUtc).InZone(timeZone).Date;
        DateTime? best = null;

        for (var dayOffset = 0; dayOffset <= 1; dayOffset++)
        {
            var date = localDate.PlusDays(-dayOffset);
            var flag = ToDayOfWeekFlag(date.DayOfWeek);

            foreach (var schedule in medication.Schedules.Where(s => s.IsActive))
            {
                if ((schedule.DaysOfWeek & flag) == 0)
                    continue;

                var slotUtc = timeZone.AtLeniently(ToLocalDateTime(date, schedule.LocalTime)).ToDateTimeUtc();
                if (slotUtc <= occurredUtc && (best is null || slotUtc > best.Value))
                    best = slotUtc;
            }
        }

        return best ?? occurredUtc;
    }

    private static DoseEvent NewMissedEvent(Guid deviceId, Slot slot, DateTime nowUtc, string note) => new()
    {
        Id = Guid.NewGuid(),
        DeviceId = deviceId,
        MedicationId = slot.MedicationId,
        ScheduledUtc = slot.ScheduledUtc,
        Type = DoseEventType.Missed,
        OccurredAtUtc = nowUtc,
        Note = note
    };

    private static DayOfWeekFlags ToDayOfWeekFlag(IsoDayOfWeek day) => day switch
    {
        IsoDayOfWeek.Monday => DayOfWeekFlags.Mon,
        IsoDayOfWeek.Tuesday => DayOfWeekFlags.Tue,
        IsoDayOfWeek.Wednesday => DayOfWeekFlags.Wed,
        IsoDayOfWeek.Thursday => DayOfWeekFlags.Thu,
        IsoDayOfWeek.Friday => DayOfWeekFlags.Fri,
        IsoDayOfWeek.Saturday => DayOfWeekFlags.Sat,
        IsoDayOfWeek.Sunday => DayOfWeekFlags.Sun,
        _ => DayOfWeekFlags.None
    };

    private sealed record Slot(Guid MedicationId, DateTime ScheduledUtc, string MedicationName, string MedicationDosage);
}