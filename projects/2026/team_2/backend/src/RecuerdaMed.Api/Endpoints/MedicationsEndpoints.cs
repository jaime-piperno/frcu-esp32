using Microsoft.EntityFrameworkCore;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

public sealed record MedicationRequest(
    Guid DeviceId, string Name, decimal DosageAmount, DoseUnit DosageUnit, bool IsActive, List<ScheduleRequest> Schedules);

public sealed record ScheduleRequest(TimeOnly LocalTime, DayOfWeekFlags DaysOfWeek, bool IsActive, string? Message);

public sealed record UpdateMedicationRequest(string? Name, decimal? DosageAmount, DoseUnit? DosageUnit, bool? IsActive);

public sealed record MedicationResponse(
    Guid Id, string Name, decimal DosageAmount, DoseUnit DosageUnit, bool IsActive, IReadOnlyList<ScheduleResponse> Schedules);

public sealed record ScheduleResponse(Guid Id, TimeOnly LocalTime, DayOfWeekFlags DaysOfWeek, bool IsActive, string? Message);

public static class MedicationsEndpoints
{
    public static WebApplication MapMedicationEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/medications").WithTags("Medications");

        group.MapGet("/", ListAsync);
        group.MapPost("/", CreateAsync);
        group.MapGet("/{id:guid}", GetAsync);
        group.MapPut("/{id:guid}", UpdateAsync);
        group.MapDelete("/{id:guid}", SoftDeleteAsync);

        group.MapGet("/{id:guid}/schedules", ListSchedulesAsync);
        group.MapPost("/{id:guid}/schedules", AddScheduleAsync);
        group.MapPut("/{id:guid}/schedules/{scheduleId:guid}", UpdateScheduleAsync);
        group.MapDelete("/{id:guid}/schedules/{scheduleId:guid}", SoftDeleteScheduleAsync);

        return app;
    }

    private static async Task<IResult> ListAsync(Guid? deviceId, AppDbContext db, CancellationToken ct)
    {
        var query = db.Medications.AsNoTracking().Where(m => m.IsActive);
        if (deviceId.HasValue)
            query = query.Where(m => m.DeviceId == deviceId.Value);

        var medications = await query
            .OrderBy(m => m.Name)
            .Select(m => new MedicationResponse(
                m.Id, m.Name, m.DosageAmount, m.DosageUnit, m.IsActive,
                m.Schedules
                    .Where(s => s.IsActive)
                    .OrderBy(s => s.LocalTime)
                    .Select(s => new ScheduleResponse(s.Id, s.LocalTime, s.DaysOfWeek, s.IsActive, s.Message))
                    .ToList()))
            .ToListAsync(ct);

        return Results.Ok(medications);
    }

    private static async Task<IResult> CreateAsync(
        MedicationRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var errors = ValidateMedicationRequest(request);
        if (errors.Count > 0)
            return Results.ValidationProblem(errors);

        var deviceExists = await db.Devices.AsNoTracking()
            .AnyAsync(d => d.Id == request.DeviceId && d.IsActive, ct);
        if (!deviceExists)
            return Results.NotFound();

        var medication = new Medication
        {
            Id = Guid.NewGuid(),
            DeviceId = request.DeviceId,
            Name = request.Name.Trim(),
            DosageAmount = request.DosageAmount,
            DosageUnit = request.DosageUnit,
            IsActive = request.IsActive,
            Schedules = request.Schedules.Select(s => new DoseSchedule
            {
                Id = Guid.NewGuid(),
                LocalTime = s.LocalTime,
                DaysOfWeek = s.DaysOfWeek,
                Message = NormalizeMessage(s.Message),
                IsActive = s.IsActive
            }).ToList()
        };

        db.Medications.Add(medication);
        await db.SaveChangesAsync(ct);

        // New schedules change when the device state will change: wake the publisher.
        signal.Notify();

        var response = await ToDetailResponseAsync(db, medication.Id, ct);
        return Results.Created($"/api/medications/{medication.Id}", response);
    }

    private static async Task<IResult> GetAsync(Guid id, AppDbContext db, CancellationToken ct)
    {
        var medication = await ToDetailResponseAsync(db, id, ct);
        return medication is null ? Results.NotFound() : Results.Ok(medication);
    }

    private static async Task<IResult> UpdateAsync(
        Guid id, UpdateMedicationRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var errors = new Dictionary<string, string[]>();
        if (request.Name is not null && string.IsNullOrWhiteSpace(request.Name))
            errors["name"] = ["Name cannot be empty."];
        if (request.DosageAmount.HasValue && !ValidDosageAmount(request.DosageAmount.Value))
            errors["dosageAmount"] = ["Dose must be greater than zero, at most 9999999.999, with up to three decimals."];
        if (request.DosageUnit.HasValue && !Enum.IsDefined(request.DosageUnit.Value))
            errors["dosageUnit"] = ["Dose unit must be Milligrams or Tablets."];

        if (errors.Count > 0)
            return Results.ValidationProblem(errors);

        var medication = await db.Medications.FirstOrDefaultAsync(m => m.Id == id && m.IsActive, ct);
        if (medication is null)
            return Results.NotFound();

        if (request.Name is not null)
            medication.Name = request.Name.Trim();
        if (request.DosageAmount.HasValue)
            medication.DosageAmount = request.DosageAmount.Value;
        if (request.DosageUnit.HasValue)
            medication.DosageUnit = request.DosageUnit.Value;
        if (request.IsActive.HasValue)
            medication.IsActive = request.IsActive.Value;

        await db.SaveChangesAsync(ct);

        // Medication changes affect slot computation: wake the publisher.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<IResult> SoftDeleteAsync(
        Guid id, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var medication = await db.Medications.FirstOrDefaultAsync(m => m.Id == id && m.IsActive, ct);
        if (medication is null)
            return Results.NotFound();

        medication.IsActive = false;
        await db.SaveChangesAsync(ct);

        // Removed slots must stop changing the device state: wake the publisher.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<IResult> ListSchedulesAsync(Guid id, AppDbContext db, CancellationToken ct)
    {
        var medicationExists = await db.Medications.AsNoTracking().AnyAsync(m => m.Id == id, ct);
        if (!medicationExists)
            return Results.NotFound();

        var schedules = await db.DoseSchedules.AsNoTracking()
            .Where(s => s.MedicationId == id && s.IsActive)
            .OrderBy(s => s.LocalTime)
            .Select(s => new ScheduleResponse(s.Id, s.LocalTime, s.DaysOfWeek, s.IsActive, s.Message))
            .ToListAsync(ct);

        return Results.Ok(schedules);
    }

    private static async Task<IResult> AddScheduleAsync(
        Guid id, ScheduleRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var errors = ValidateSchedule(request);
        if (errors.Count > 0) return Results.ValidationProblem(errors);

        var medication = await db.Medications.FirstOrDefaultAsync(m => m.Id == id && m.IsActive, ct);
        if (medication is null)
            return Results.NotFound();

        var schedule = new DoseSchedule
        {
            Id = Guid.NewGuid(),
            MedicationId = id,
            LocalTime = request.LocalTime,
            DaysOfWeek = request.DaysOfWeek,
            Message = NormalizeMessage(request.Message),
            IsActive = request.IsActive
        };

        db.DoseSchedules.Add(schedule);
        await db.SaveChangesAsync(ct);

        // A new schedule changes when the device state will change: wake the publisher.
        signal.Notify();

        return Results.Created($"/api/medications/{id}/schedules/{schedule.Id}",
            new ScheduleResponse(schedule.Id, schedule.LocalTime, schedule.DaysOfWeek, schedule.IsActive, schedule.Message));
    }

    private static async Task<IResult> UpdateScheduleAsync(
        Guid id, Guid scheduleId, ScheduleRequest request, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var errors = ValidateSchedule(request);
        if (errors.Count > 0) return Results.ValidationProblem(errors);

        var schedule = await db.DoseSchedules
            .FirstOrDefaultAsync(s => s.Id == scheduleId && s.MedicationId == id, ct);
        if (schedule is null)
            return Results.NotFound();

        schedule.LocalTime = request.LocalTime;
        schedule.DaysOfWeek = request.DaysOfWeek;
        schedule.Message = NormalizeMessage(request.Message);
        schedule.IsActive = request.IsActive;

        await db.SaveChangesAsync(ct);

        // The schedule change affects slot computation: wake the publisher.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<IResult> SoftDeleteScheduleAsync(
        Guid id, Guid scheduleId, AppDbContext db, StateChangeSignal signal, CancellationToken ct)
    {
        var schedule = await db.DoseSchedules
            .FirstOrDefaultAsync(s => s.Id == scheduleId && s.MedicationId == id, ct);
        if (schedule is null)
            return Results.NotFound();

        schedule.IsActive = false;
        await db.SaveChangesAsync(ct);

        // Removed slots must stop changing the device state: wake the publisher.
        signal.Notify();

        return Results.NoContent();
    }

    private static async Task<MedicationResponse?> ToDetailResponseAsync(AppDbContext db, Guid id, CancellationToken ct)
        => await db.Medications.AsNoTracking()
            .Where(m => m.Id == id)
            .Select(m => new MedicationResponse(
                m.Id, m.Name, m.DosageAmount, m.DosageUnit, m.IsActive,
                m.Schedules
                    .OrderBy(s => s.LocalTime)
                    .Select(s => new ScheduleResponse(s.Id, s.LocalTime, s.DaysOfWeek, s.IsActive, s.Message))
                    .ToList()))
            .FirstOrDefaultAsync(ct);

    private static Dictionary<string, string[]> ValidateMedicationRequest(MedicationRequest request)
    {
        var errors = new Dictionary<string, string[]>();

        if (request.DeviceId == Guid.Empty)
            errors["deviceId"] = ["DeviceId is required."];
        if (string.IsNullOrWhiteSpace(request.Name))
            errors["name"] = ["Name is required."];
        if (!ValidDosageAmount(request.DosageAmount))
            errors["dosageAmount"] = ["Dose must be greater than zero, at most 9999999.999, with up to three decimals."];
        if (!Enum.IsDefined(request.DosageUnit))
            errors["dosageUnit"] = ["Dose unit must be Milligrams or Tablets."];

        if (request.Schedules is null || request.Schedules.Count == 0)
        {
            errors["schedules"] = ["At least one schedule is required."];
        }
        else
        {
            for (var i = 0; i < request.Schedules.Count; i++)
            {
                foreach (var error in ValidateSchedule(request.Schedules[i]))
                    errors[$"schedules[{i}].{error.Key}"] = error.Value;
            }
        }

        return errors;
    }

    private static bool ValidDosageAmount(decimal amount)
        => amount > 0 && amount <= 9_999_999.999m && decimal.Round(amount, 3) == amount;

    private static string? NormalizeMessage(string? message)
        => string.IsNullOrWhiteSpace(message) ? null : message.Trim();

    private static Dictionary<string, string[]> ValidateSchedule(ScheduleRequest request)
    {
        var errors = new Dictionary<string, string[]>();
        if (request.DaysOfWeek == DayOfWeekFlags.None)
            errors["daysOfWeek"] = ["At least one day of week must be selected."];
        if (NormalizeMessage(request.Message)?.Length > 300)
            errors["message"] = ["Message cannot exceed 300 characters."];
        return errors;
    }
}
