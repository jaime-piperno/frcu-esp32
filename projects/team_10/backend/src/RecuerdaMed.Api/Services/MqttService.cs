using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Protocol;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Persistence;

namespace RecuerdaMed.Api.Services;

public sealed class MqttOptions
{
    public const string SectionName = "Mqtt";

    public string Broker { get; set; } = "localhost";
    public int Port { get; set; } = 1883;
    public string ClientId { get; set; } = "recuerdamed-backend";
    public int ReconnectSeconds { get; set; } = 5;
    public int ConnectTimeoutSeconds { get; set; } = 10;
    public int StatePublishIntervalSeconds { get; set; } = 30;
    public int WakeMarginSeconds { get; set; } = 2;
}

/// <summary>Payload published by the ESP32 on recuerdamed/&lt;deviceId&gt;/events and
/// accepted by the REST POST /api/devices/{id}/events.</summary>
public sealed record DeviceEventRequest
{
    public string Type { get; init; } = string.Empty;
    public Guid MedicationId { get; init; }
    public DateTimeOffset? OccurredAtUtc { get; init; }
    public int? SnoozeSeconds { get; init; }

    public DoseEventType? ToDoseEventType()
        => Type.Trim().ToLowerInvariant() switch
        {
            "taken" => DoseEventType.Taken,
            "snoozed" => DoseEventType.Snoozed,
            _ => null
        };
}

/// <summary>
/// Maintains the MQTT link to the broker with auto-reconnect. Publishes the device
/// StateMessage to recuerdamed/&lt;deviceId&gt;/state (RETAINED, QoS 1) on every change
/// and subscribes to recuerdamed/+/events to ingest device events through the same
/// path as the REST API.
/// </summary>
public sealed class MqttService : BackgroundService
{
    private const string TopicPrefix = "recuerdamed";
    private const string DeviceEventsTopicFilter = "recuerdamed/+/events";

    internal static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private readonly IServiceScopeFactory _scopeFactory;
    private readonly IOptions<MqttOptions> _options;
    private readonly StateChangeSignal _signal;
    private readonly ILogger<MqttService> _logger;
    private IMqttClient? _client;

    public MqttService(
        IServiceScopeFactory scopeFactory,
        IOptions<MqttOptions> options,
        StateChangeSignal signal,
        ILogger<MqttService> logger)
    {
        _scopeFactory = scopeFactory;
        _options = options;
        _signal = signal;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var factory = new MqttFactory();
        _client = factory.CreateMqttClient();
        _client.ApplicationMessageReceivedAsync += OnApplicationMessageReceivedAsync;
        _client.DisconnectedAsync += OnDisconnectedAsync;

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                if (!_client.IsConnected)
                {
                    await ConnectAndSubscribeAsync(stoppingToken);
                    await PublishAllDeviceStatesAsync(stoppingToken);
                }
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex,
                    "MQTT broker unavailable at {Broker}:{Port}; retrying in {Delay}s",
                    _options.Value.Broker, _options.Value.Port, _options.Value.ReconnectSeconds);
            }

            try
            {
                await Task.Delay(TimeSpan.FromSeconds(_options.Value.ReconnectSeconds), stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }

        if (_client is not null)
        {
            try
            {
                await _client.DisconnectAsync();
            }
            catch
            {
                // Ignore disconnect errors on shutdown.
            }

            _client.Dispose();
        }
    }

    private async Task ConnectAndSubscribeAsync(CancellationToken ct)
    {
        var opts = _options.Value;

        // Unique client id per instance.
        var clientId = $"{opts.ClientId}-{Guid.NewGuid():N}";
        if (clientId.Length > 40)
            clientId = clientId[..40];

        var mqttOptions = new MqttClientOptionsBuilder()
            .WithTcpServer(opts.Broker, opts.Port)
            .WithClientId(clientId)
            .WithCleanSession(true)
            .WithTimeout(TimeSpan.FromSeconds(opts.ConnectTimeoutSeconds))
            .Build();

        await _client!.ConnectAsync(mqttOptions, ct);
        await _client.SubscribeAsync(new MqttClientSubscribeOptionsBuilder()
            .WithTopicFilter(DeviceEventsTopicFilter, MqttQualityOfServiceLevel.AtLeastOnce)
            .Build(), ct);

        _logger.LogInformation("Connected to MQTT broker {Broker}:{Port} (client {ClientId})",
            opts.Broker, opts.Port, clientId);
    }

    private Task OnDisconnectedAsync(MqttClientDisconnectedEventArgs e)
    {
        if (e.ClientWasConnected)
            _logger.LogWarning("MQTT disconnected: {Reason}", e.Reason);

        return Task.CompletedTask;
    }

    private async Task OnApplicationMessageReceivedAsync(MqttApplicationMessageReceivedEventArgs e)
    {
        var segments = e.ApplicationMessage.Topic.Split('/');
        if (segments.Length != 3
            || !string.Equals(segments[0], TopicPrefix, StringComparison.Ordinal)
            || !string.Equals(segments[2], "events", StringComparison.Ordinal)
            || !Guid.TryParse(segments[1], out var deviceId))
        {
            return;
        }

        try
        {
            var request = JsonSerializer.Deserialize<DeviceEventRequest>(e.ApplicationMessage.PayloadSegment, JsonOptions);
            if (request is null
                || string.IsNullOrWhiteSpace(request.Type)
                || request.MedicationId == Guid.Empty
                || request.ToDoseEventType() is not { } type)
            {
                _logger.LogWarning("Ignoring malformed device event on topic {Topic}", e.ApplicationMessage.Topic);
                return;
            }

            using var scope = _scopeFactory.CreateScope();
            var alerts = scope.ServiceProvider.GetRequiredService<AlertStateService>();
            var result = await alerts.RecordEventAsync(
                deviceId, type, request.MedicationId, request.OccurredAtUtc, request.SnoozeSeconds);

            await PublishStateAsync(deviceId, result.State);

            // Push the event to connected dashboards over SignalR (same path as REST).
            var notifier = scope.ServiceProvider.GetRequiredService<AdherenceNotifier>();
            await notifier.NotifyEventAsync(deviceId, AdherenceNotifier.EventPayload(result.Event));

            // The event changed device state: wake the publisher so it recomputes its
            // next wake-up (e.g. a snooze at 08:00 must re-awaken at 08:05).
            _signal.Notify();
        }
        catch (NotFoundException nf)
        {
            _logger.LogWarning(nf, "Ignoring device event for unknown device or medication");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to process device event from topic {Topic}", e.ApplicationMessage.Topic);
        }
    }

    /// <summary>Publishes the device state (RETAINED, QoS 1) on recuerdamed/{deviceId}/state.
    /// Returns true when the message was actually sent to a connected broker.</summary>
    public async Task<bool> PublishStateAsync(Guid deviceId, StateMessage state, CancellationToken ct = default)
    {
        if (_client is null || !_client.IsConnected)
        {
            _logger.LogDebug("MQTT not connected; skipping state publish for device {DeviceId}", deviceId);
            return false;
        }

        var message = new MqttApplicationMessageBuilder()
            .WithTopic($"{TopicPrefix}/{deviceId}/state")
            .WithPayload(JsonSerializer.Serialize(state, JsonOptions))
            .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithRetainFlag()
            .Build();

        try
        {
            await _client.PublishAsync(message, ct);
            return true;
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to publish state for device {DeviceId}", deviceId);
            return false;
        }
    }

    private async Task PublishAllDeviceStatesAsync(CancellationToken ct)
    {
        using var scope = _scopeFactory.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();
        var deviceIds = await db.Devices.AsNoTracking()
            .Where(d => d.IsActive)
            .Select(d => d.Id)
            .ToListAsync(ct);

        foreach (var deviceId in deviceIds)
        {
            var alerts = scope.ServiceProvider.GetRequiredService<AlertStateService>();
            var state = await alerts.UpdateAndGetStateAsync(deviceId, ct);
            await PublishStateAsync(deviceId, state, ct);
        }
    }
}