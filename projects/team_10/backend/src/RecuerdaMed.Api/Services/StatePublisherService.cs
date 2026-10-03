using System.Collections.Concurrent;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using RecuerdaMed.Api.Persistence;

namespace RecuerdaMed.Api.Services;

/// <summary>
/// Periodically recomputes every active device state and publishes it over MQTT
/// whenever it changed. This is what lets the ESP32 learn that a dose slot just
/// became alerting without polling: the retained state on
/// recuerdamed/&lt;deviceId&gt;/state is refreshed as soon as the computed state differs.
/// </summary>
public sealed class StatePublisherService : BackgroundService
{
    private readonly IServiceScopeFactory _scopeFactory;
    private readonly MqttService _mqtt;
    private readonly AdherenceNotifier _notifier;
    private readonly IOptions<MqttOptions> _options;
    private readonly StateChangeSignal _signal;
    private readonly ILogger<StatePublisherService> _logger;
    private readonly ConcurrentDictionary<Guid, string> _lastPublished = new();

    public StatePublisherService(
        IServiceScopeFactory scopeFactory,
        MqttService mqtt,
        AdherenceNotifier notifier,
        IOptions<MqttOptions> options,
        StateChangeSignal signal,
        ILogger<StatePublisherService> logger)
    {
        _scopeFactory = scopeFactory;
        _mqtt = mqtt;
        _notifier = notifier;
        _options = options;
        _signal = signal;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await PublishChangedStatesAsync(stoppingToken);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "State publisher tick failed");
            }

            try
            {
                var delay = await ComputeNextWakeDelayAsync(stoppingToken);
                await Task.WhenAny(Task.Delay(delay, stoppingToken), _signal.WaitAsync(stoppingToken));
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    /// <summary>
    /// Computes how long to sleep until the earliest moment any active device state
    /// will change on its own (a slot becoming alerting, a snooze expiring, or a
    /// missed window closing). Falls back to <see cref="MqttOptions.StatePublishIntervalSeconds"/>
    /// when nothing is scheduled, and caps the sleep at that same interval as a safety
    /// net in case the prediction is wrong.
    /// </summary>
    private async Task<TimeSpan> ComputeNextWakeDelayAsync(CancellationToken ct)
    {
        var fallback = TimeSpan.FromSeconds(_options.Value.StatePublishIntervalSeconds);

        try
        {
            using var scope = _scopeFactory.CreateScope();
            var alertState = scope.ServiceProvider.GetRequiredService<AlertStateService>();
            var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();

            var deviceIds = await db.Devices.AsNoTracking()
                .Where(d => d.IsActive)
                .Select(d => d.Id)
                .ToListAsync(ct);

            DateTime? next = null;
            foreach (var deviceId in deviceIds)
            {
                var stateChange = await alertState.GetNextStateChangeUtcAsync(deviceId, ct);
                if (stateChange.HasValue && (!next.HasValue || stateChange.Value < next.Value))
                    next = stateChange;
            }

            if (next is null)
                return fallback;

            var delay = next.Value - DateTime.UtcNow
                        + TimeSpan.FromSeconds(_options.Value.WakeMarginSeconds);

            if (delay < TimeSpan.FromMilliseconds(500))
                delay = TimeSpan.FromMilliseconds(500);
            if (delay > fallback)
                delay = fallback;

            return delay;
        }
        catch (Exception ex)
        {
            // Never let a prediction failure kill the loop; fall back to the idle interval.
            _logger.LogWarning(ex, "Failed to compute next state-change wake-up; falling back to the idle interval");
            return fallback;
        }
    }

    private async Task PublishChangedStatesAsync(CancellationToken ct)
    {
        using var scope = _scopeFactory.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();
        var alertState = scope.ServiceProvider.GetRequiredService<AlertStateService>();

        var deviceIds = await db.Devices.AsNoTracking()
            .Where(d => d.IsActive)
            .Select(d => d.Id)
            .ToListAsync(ct);

        foreach (var deviceId in deviceIds)
        {
            var state = await alertState.UpdateAndGetStateAsync(deviceId, ct);
            var payload = JsonSerializer.Serialize(state, MqttService.JsonOptions);

            // Publish only when the computed state changed since the last tick.
            if (_lastPublished.TryGetValue(deviceId, out var previous) && previous == payload)
                continue;

            var sent = await _mqtt.PublishStateAsync(deviceId, state, ct);
            if (!sent)
                continue;

            _lastPublished[deviceId] = payload;
            await _notifier.NotifyStateAsync(deviceId, state, ct);
            _logger.LogInformation("Published changed state for device {DeviceId} ({Alerts} alerts, {Snoozed}, {Missed})",
                deviceId, state.Alerts.Count, state.Snoozed, state.Missed);
        }
    }
}