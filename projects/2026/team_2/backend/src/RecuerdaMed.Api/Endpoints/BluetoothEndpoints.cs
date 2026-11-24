using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

public sealed record BluetoothSelectionRequest(string? Address, string? Name);

public sealed record BluetoothDeviceResponse(
    string? SelectedAddress,
    string? SelectedName,
    string ConnectionStatus,
    string ScanStatus,
    string? ConnectedAddress,
    string? Error,
    IReadOnlyList<BluetoothDiscoveredDevice> Devices,
    DateTime? UpdatedAtUtc,
    DateTime? ScanStartedAtUtc,
    DateTime? ConnectionUpdatedAtUtc);

public static partial class BluetoothEndpoints
{
    [GeneratedRegex("^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")]
    private static partial Regex BluetoothAddressRegex();

    public static WebApplication MapBluetoothEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/devices/{deviceId:guid}/bluetooth").WithTags("Bluetooth");
        group.MapGet("/", GetAsync);
        group.MapPost("/scan", ScanAsync);
        group.MapPut("/speaker", SelectSpeakerAsync);
        return app;
    }

    private static async Task<IResult> GetAsync(Guid deviceId, AppDbContext db, BluetoothStatusService status, CancellationToken ct)
    {
        var device = await db.Devices.AsNoTracking().FirstOrDefaultAsync(d => d.Id == deviceId && d.IsActive, ct);
        return device is null ? Results.NotFound() : Results.Ok(ToResponse(device, status.Get(deviceId)));
    }

    private static async Task<IResult> ScanAsync(Guid deviceId, AppDbContext db, MqttService mqtt, BluetoothStatusService status, CancellationToken ct)
    {
        if (!await db.Devices.AsNoTracking().AnyAsync(d => d.Id == deviceId && d.IsActive, ct))
            return Results.NotFound();
        if (!await mqtt.PublishBluetoothScanAsync(deviceId, ct))
            return Results.Problem("El broker MQTT no está disponible.", statusCode: StatusCodes.Status503ServiceUnavailable);
        status.MarkScanRequested(deviceId);
        return Results.Accepted($"/api/devices/{deviceId}/bluetooth/");
    }

    private static async Task<IResult> SelectSpeakerAsync(
        Guid deviceId, BluetoothSelectionRequest request, AppDbContext db,
        MqttService mqtt, BluetoothStatusService status, CancellationToken ct)
    {
        var address = request.Address?.Trim().ToUpperInvariant();
        var name = request.Name?.Trim();
        if (address is not null && !BluetoothAddressRegex().IsMatch(address))
            return Results.ValidationProblem(new Dictionary<string, string[]> { ["address"] = ["Dirección Bluetooth inválida."] });
        if (address is not null && (string.IsNullOrWhiteSpace(name) || name.Length > 100))
            return Results.ValidationProblem(new Dictionary<string, string[]> { ["name"] = ["El nombre debe tener entre 1 y 100 caracteres."] });

        var device = await db.Devices.FirstOrDefaultAsync(d => d.Id == deviceId && d.IsActive, ct);
        if (device is null) return Results.NotFound();

        device.BluetoothSpeakerAddress = address;
        device.BluetoothSpeakerName = address is null ? null : name;
        await db.SaveChangesAsync(ct);
        var commandPublished = await mqtt.PublishBluetoothConfigurationAsync(device, ct);
        return Results.Ok(new { bluetooth = ToResponse(device, status.Get(deviceId)), commandPublished });
    }

    private static BluetoothDeviceResponse ToResponse(RecuerdaMed.Api.Domain.Device device, BluetoothLiveStatus live)
        => new(device.BluetoothSpeakerAddress, device.BluetoothSpeakerName,
            live.ConnectionStatus, live.ScanStatus, live.ConnectedAddress,
            live.Error, live.Devices, live.UpdatedAtUtc, live.ScanStartedAtUtc, live.ConnectionUpdatedAtUtc);
}
