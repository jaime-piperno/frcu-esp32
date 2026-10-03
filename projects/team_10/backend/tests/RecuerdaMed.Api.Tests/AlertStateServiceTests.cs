using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using NodaTime;
using NodaTime.Testing;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;
using Xunit;

namespace RecuerdaMed.Api.Tests;

public sealed class AlertStateServiceTests
{
    private const string BuenosAires = "America/Argentina/Buenos_Aires";
    private const DayOfWeekFlags AllDays = DayOfWeekFlags.Mon | DayOfWeekFlags.Tue | DayOfWeekFlags.Wed
        | DayOfWeekFlags.Thu | DayOfWeekFlags.Fri | DayOfWeekFlags.Sat | DayOfWeekFlags.Sun;

    // 2026-09-07 is a Monday, 2026-09-08 a Tuesday, 2026-09-09 a Wednesday, 2026-09-10 a Thursday.
    private static readonly DateOnly Wednesday = new(2026, 9, 9);
    private static readonly DateOnly Thursday = new(2026, 9, 10);

    private static (AppDbContext Db, AlertStateService Service, FakeClock Clock) CreateHarness(DateTime utcNow)
    {
        var options = new DbContextOptionsBuilder<AppDbContext>()
            .UseInMemoryDatabase($"recuerdamed-unit-{Guid.NewGuid():N}")
            .Options;

        var db = new AppDbContext(options);
        var clock = new FakeClock(Instant.FromDateTimeUtc(DateTime.SpecifyKind(utcNow, DateTimeKind.Utc)));
        var service = new AlertStateService(db, Options.Create(new AlertingOptions()), clock);
        return (db, service, clock);
    }

    private static async Task<(Guid DeviceId, Guid MedicationId)> SeedMedicationAsync(
        AppDbContext db,
        params (TimeOnly LocalTime, DayOfWeekFlags Days)[] schedules)
    {
        var device = new Device
        {
            Id = Guid.NewGuid(),
            Name = "Test device",
            ApiKey = "test-key",
            TimeZoneId = BuenosAires,
            CreatedAtUtc = DateTime.UtcNow
        };

        var medication = new Medication
        {
            Id = Guid.NewGuid(),
            DeviceId = device.Id,
            Name = "Losartan",
            Dosage = "50mg",
            Schedules = schedules
                .Select(s => new DoseSchedule { Id = Guid.NewGuid(), LocalTime = s.LocalTime, DaysOfWeek = s.Days })
                .ToList()
        };

        db.Devices.Add(device);
        db.Medications.Add(medication);
        await db.SaveChangesAsync();

        return (device.Id, medication.Id);
    }

    private static DateTime LocalToUtc(DateOnly date, TimeOnly time, string timeZoneId = BuenosAires)
        => DateTimeZoneProviders.Tzdb[timeZoneId]
            .AtLeniently(AlertStateService.ToLocalDateTime(new LocalDate(date.Year, date.Month, date.Day), time))
            .ToDateTimeUtc();

    [Fact]
    public async Task Alert_fires_at_schedule_time()
    {
        var now = LocalToUtc(Thursday, new TimeOnly(8, 0));
        var (db, service, _) = CreateHarness(now);
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // Mark yesterday's slot as taken so the harness starts without a historical missed.
        await service.RecordEventAsync(deviceId, DoseEventType.Taken, medicationId,
            new DateTimeOffset(LocalToUtc(Wednesday, new TimeOnly(8, 0))));

        var state = await service.UpdateAndGetStateAsync(deviceId);

        Assert.Contains(state.Alerts, a => a.MedicationId == medicationId);
        Assert.Equal(1, state.Alerts.Count);
        Assert.False(state.Snoozed);
        Assert.False(state.Missed);
    }

    [Fact]
    public async Task No_alert_before_schedule_time()
    {
        var now = LocalToUtc(Thursday, new TimeOnly(7, 59));
        var (db, service, _) = CreateHarness(now);
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        var state = await service.UpdateAndGetStateAsync(deviceId);

        Assert.Empty(state.Alerts);
        Assert.Contains(state.NextDoses, n => n.MedicationId == medicationId);
    }

    [Fact]
    public async Task Snooze_suppresses_alert_and_realerts_after_5_minutes()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        var initial = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Contains(initial.Alerts, a => a.MedicationId == medicationId);

        // Snooze at 08:00 (default 5 minutes).
        await service.RecordEventAsync(deviceId, DoseEventType.Snoozed, medicationId);

        // Still inside the snooze window -> snoozed, not alerting.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 1))));
        var during = await service.UpdateAndGetStateAsync(deviceId);
        Assert.True(during.Snoozed);
        Assert.Empty(during.Alerts);

        // After the 5-minute snooze expires -> alerting again.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 6))));
        var after = await service.UpdateAndGetStateAsync(deviceId);
        Assert.False(after.Snoozed);
        Assert.Contains(after.Alerts, a => a.MedicationId == medicationId);
    }

    [Fact]
    public async Task Third_snooze_expiry_registers_missed()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // Mark yesterday's slot as taken so the harness starts without a historical missed.
        await service.RecordEventAsync(deviceId, DoseEventType.Taken, medicationId,
            new DateTimeOffset(LocalToUtc(Wednesday, new TimeOnly(8, 0))));

        // Snooze #1 at 08:00.
        await service.RecordEventAsync(deviceId, DoseEventType.Snoozed, medicationId);

        // Snooze #2 at 08:06 (first snooze expired).
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 6))));
        var reAlert = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Contains(reAlert.Alerts, a => a.MedicationId == medicationId);
        await service.RecordEventAsync(deviceId, DoseEventType.Snoozed, medicationId);

        // Snooze #3 at 08:12.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 12))));
        await service.RecordEventAsync(deviceId, DoseEventType.Snoozed, medicationId);

        // Third snooze expires at 08:18 without a Taken -> Missed.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 18))));
        var state = await service.UpdateAndGetStateAsync(deviceId);

        Assert.True(state.Missed);
        Assert.Empty(state.Alerts);

        var missedCount = await db.DoseEvents
            .CountAsync(e => e.MedicationId == medicationId && e.Type == DoseEventType.Missed);
        Assert.Equal(1, missedCount);

        var snoozeCount = await db.DoseEvents
            .CountAsync(e => e.MedicationId == medicationId && e.Type == DoseEventType.Snoozed);
        Assert.Equal(3, snoozeCount);
    }

    [Fact]
    public async Task No_response_within_4_hours_registers_missed_idempotently()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // Mark yesterday's slot as taken so the harness starts without a historical missed.
        await service.RecordEventAsync(deviceId, DoseEventType.Taken, medicationId,
            new DateTimeOffset(LocalToUtc(Wednesday, new TimeOnly(8, 0))));

        var initial = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Contains(initial.Alerts, a => a.MedicationId == medicationId);

        // 4 hours later, still no response.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(12, 0))));
        var state = await service.UpdateAndGetStateAsync(deviceId);

        Assert.True(state.Missed);
        Assert.Empty(state.Alerts);

        // Calling again must not duplicate the Missed event.
        var again = await service.UpdateAndGetStateAsync(deviceId);
        Assert.True(again.Missed);

        var missedCount = await db.DoseEvents
            .CountAsync(e => e.MedicationId == medicationId && e.Type == DoseEventType.Missed);
        Assert.Equal(1, missedCount);
    }

    [Fact]
    public async Task Multiple_schedules_for_one_medication_are_independent()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 5)));
        var (deviceId, medicationId) = await SeedMedicationAsync(
            db,
            (new TimeOnly(8, 0), AllDays),
            (new TimeOnly(20, 0), AllDays));

        var morning = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Equal(1, morning.Alerts.Count);
        Assert.Contains(morning.Alerts, a => a.MedicationId == medicationId);
        Assert.Contains(morning.NextDoses, n => n.ScheduledUtc == LocalToUtc(Thursday, new TimeOnly(20, 0)));

        // At 20:05 only the evening slot is alerting; the morning slot is now missed.
        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(20, 5))));
        var evening = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Equal(1, evening.Alerts.Count);
        Assert.Contains(evening.Alerts, a => a.MedicationId == medicationId);
        Assert.True(evening.Missed);
    }

    [Fact]
    public async Task Days_of_week_are_respected()
    {
        var monday = new DateOnly(2026, 9, 7);
        var tuesday = new DateOnly(2026, 9, 8);

        var (db, service, clock) = CreateHarness(LocalToUtc(monday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), DayOfWeekFlags.Mon));

        var onMonday = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Contains(onMonday.Alerts, a => a.MedicationId == medicationId);

        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(tuesday, new TimeOnly(8, 0))));
        var onTuesday = await service.UpdateAndGetStateAsync(deviceId);
        Assert.Empty(onTuesday.Alerts);
        // The medication only fires on Mondays: the next dose is next Monday, not today.
        Assert.Contains(onTuesday.NextDoses, n => n.MedicationId == medicationId
            && n.ScheduledUtc == LocalToUtc(new DateOnly(2026, 9, 14), new TimeOnly(8, 0)));
    }

    [Fact]
    public async Task Time_zone_conversion_0800_buenos_aires_is_1100_utc()
    {
        var (db, service, _) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(0, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        var state = await service.UpdateAndGetStateAsync(deviceId);

        var expected = new DateTime(2026, 9, 10, 11, 0, 0, DateTimeKind.Utc);
        Assert.Contains(state.NextDoses, n => n.MedicationId == medicationId && n.ScheduledUtc == expected);
    }

    [Fact]
    public async Task Next_change_is_the_future_slot_time()
    {
        var now = LocalToUtc(Thursday, new TimeOnly(7, 59));
        var (db, service, _) = CreateHarness(now);
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        var next = await service.GetNextStateChangeUtcAsync(deviceId);

        Assert.Equal(LocalToUtc(Thursday, new TimeOnly(8, 0)), next);
    }

    [Fact]
    public async Task Returns_null_when_all_future_slots_are_taken()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(7, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // BuildSlots covers -1..+6 days from today, so take every remaining slot from
        // Thursday through next Wednesday; yesterday's past slot never changes anyway.
        for (var dayOffset = 0; dayOffset <= 6; dayOffset++)
        {
            await service.RecordEventAsync(deviceId, DoseEventType.Taken, medicationId,
                new DateTimeOffset(LocalToUtc(Thursday.AddDays(dayOffset), new TimeOnly(8, 0))));
        }

        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 1))));
        var next = await service.GetNextStateChangeUtcAsync(deviceId);

        Assert.Null(next);
    }

    [Fact]
    public async Task Next_change_is_snooze_expiry()
    {
        var (db, service, clock) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // Snooze at 08:00 (default 5 minutes) -> the state flips back at 08:05.
        await service.RecordEventAsync(deviceId, DoseEventType.Snoozed, medicationId);

        clock.Reset(Instant.FromDateTimeUtc(LocalToUtc(Thursday, new TimeOnly(8, 0, 30))));
        var next = await service.GetNextStateChangeUtcAsync(deviceId);

        Assert.Equal(LocalToUtc(Thursday, new TimeOnly(8, 5)), next);
    }

    [Fact]
    public async Task Next_change_is_missed_window_close_for_alerting_slot()
    {
        var (db, service, _) = CreateHarness(LocalToUtc(Thursday, new TimeOnly(8, 0)));
        var (deviceId, medicationId) = await SeedMedicationAsync(db, (new TimeOnly(8, 0), AllDays));

        // Mark yesterday's slot as taken so the harness starts without a historical missed.
        await service.RecordEventAsync(deviceId, DoseEventType.Taken, medicationId,
            new DateTimeOffset(LocalToUtc(Wednesday, new TimeOnly(8, 0))));

        var next = await service.GetNextStateChangeUtcAsync(deviceId);

        // 08:00 alerting with no snooze -> the only future transition is the 4-hour
        // missed window closing at 12:00.
        Assert.Equal(LocalToUtc(Thursday, new TimeOnly(12, 0)), next);
    }
}