using System.Collections.Concurrent;

namespace RecuerdaMed.Api.Services;

public sealed record BluetoothDiscoveredDevice(string Address, string Name, int Rssi, bool AudioCapable);

public sealed record BluetoothReport(
    string Type,
    string? Status,
    string? Address,
    string? Error,
    IReadOnlyList<BluetoothDiscoveredDevice>? Devices);

public sealed record BluetoothLiveStatus(
    string ConnectionStatus,
    string ScanStatus,
    string? ConnectedAddress,
    string? Error,
    IReadOnlyList<BluetoothDiscoveredDevice> Devices,
    DateTime? UpdatedAtUtc,
    DateTime? ScanStartedAtUtc,
    DateTime? ConnectionUpdatedAtUtc);

public sealed class BluetoothStatusService
{
    private readonly ConcurrentDictionary<Guid, BluetoothLiveStatus> _statuses = new();

    public BluetoothLiveStatus Get(Guid deviceId) => _statuses.GetValueOrDefault(deviceId)
        ?? new BluetoothLiveStatus("unknown", "idle", null, null, [], null, null, null);

    public void MarkScanRequested(Guid deviceId) => _statuses.AddOrUpdate(deviceId,
        _ => new BluetoothLiveStatus("unknown", "scanning", null, null, [], DateTime.UtcNow, DateTime.UtcNow, null),
        (_, current) => current with { ScanStatus = "scanning", Error = null, Devices = [], UpdatedAtUtc = DateTime.UtcNow, ScanStartedAtUtc = DateTime.UtcNow });

    public void Apply(Guid deviceId, BluetoothReport report)
    {
        _statuses.AddOrUpdate(deviceId,
            _ => ApplyReport(new BluetoothLiveStatus("unknown", "idle", null, null, [], null, null, null), report),
            (_, current) => ApplyReport(current, report));
    }

    private static BluetoothLiveStatus ApplyReport(BluetoothLiveStatus current, BluetoothReport report)
    {
        var now = DateTime.UtcNow;
        return report.Type switch
        {
            "scan" => current with
            {
                ScanStatus = report.Status is "started" ? "scanning" : report.Status is "complete" ? "complete" : "error",
                Devices = report.Status is "complete" ? report.Devices ?? [] : current.Devices,
                Error = report.Error,
                ScanStartedAtUtc = report.Status is "started" ? now : null,
                UpdatedAtUtc = now
            },
            "connection" => current with
            {
                ConnectionStatus = report.Status ?? "unknown",
                ConnectedAddress = report.Status is "connected" ? report.Address : null,
                Error = report.Error,
                ConnectionUpdatedAtUtc = now,
                UpdatedAtUtc = now
            },
            _ => current
        };
    }
}
