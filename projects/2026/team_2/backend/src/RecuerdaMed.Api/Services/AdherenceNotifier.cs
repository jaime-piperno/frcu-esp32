using Microsoft.AspNetCore.SignalR;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Hubs;

namespace RecuerdaMed.Api.Services;

public sealed class AdherenceNotifier(IHubContext<AdherenceHub> hub)
{
    /// <summary>Maps a persisted DoseEvent to the exact anonymous shape the dashboard
    /// consumes (serialized camelCase via System.Text.Json web defaults, type as a
    /// lowercase string). Single source of truth for the SignalR event payload.</summary>
    public static object EventPayload(DoseEvent e) => new
    {
        e.Id,
        e.DeviceId,
        e.MedicationId,
        e.ScheduledUtc,
        Type = e.Type.ToString().ToLowerInvariant(),
        e.OccurredAtUtc,
        e.SnoozeSeconds,
        e.Note
    };

    /// <summary>Pushes { deviceId, event } to every connected dashboard client.</summary>
    public async Task NotifyEventAsync(Guid deviceId, object payload, CancellationToken ct = default)
        => await hub.Clients.All.SendAsync("AdherenceEvent", new { DeviceId = deviceId, Event = payload }, ct);

    /// <summary>Pushes the recomputed device state to every connected dashboard client.</summary>
    public async Task NotifyStateAsync(Guid deviceId, StateMessage state, CancellationToken ct = default)
        => await hub.Clients.All.SendAsync("StateChanged", new { DeviceId = deviceId, State = state }, ct);
}