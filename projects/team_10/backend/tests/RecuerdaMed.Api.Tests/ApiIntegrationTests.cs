using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.EntityFrameworkCore;
using RecuerdaMed.Api.Persistence;
using Testcontainers.PostgreSql;
using Xunit;

namespace RecuerdaMed.Api.Tests;

[CollectionDefinition("Postgres")]
public sealed class PostgresCollection : ICollectionFixture<PostgresFixture>
{
}

public sealed class PostgresFixture : IAsyncLifetime
{
    public PostgreSqlContainer Postgres { get; } = new PostgreSqlBuilder()
        .WithImage("postgres:17-alpine")
        .WithDatabase("recuerdamed")
        .WithUsername("recuerdamed")
        .WithPassword("recuerdamed_test_password")
        .Build();

    public Task InitializeAsync() => Postgres.StartAsync();

    public Task DisposeAsync() => Postgres.DisposeAsync().AsTask();
}

[Collection("Postgres")]
public sealed class ApiIntegrationTests : IAsyncLifetime
{
    private readonly PostgresFixture _fixture;
    private WebApplicationFactory<Program>? _factory;

    public ApiIntegrationTests(PostgresFixture fixture) => _fixture = fixture;

    public async Task InitializeAsync()
    {
        _factory = CreateFactory();
        await ResetDatabaseAsync();
    }

    public async Task DisposeAsync()
    {
        if (_factory is not null)
            await _factory.DisposeAsync();
    }

    private WebApplicationFactory<Program> CreateFactory()
        => new WebApplicationFactory<Program>()
            .WithWebHostBuilder(builder =>
            {
                builder.UseSetting("ConnectionStrings:Default", _fixture.Postgres.GetConnectionString());
                builder.UseEnvironment("Testing");
                builder.UseSetting("Mqtt:Broker", "127.0.0.1");
                builder.UseSetting("Mqtt:Port", "18999");
                builder.UseSetting("Mqtt:ClientId", $"integration-test-{Guid.NewGuid():N}");
            });

    private async Task ResetDatabaseAsync()
    {
        var options = new DbContextOptionsBuilder<AppDbContext>()
            .UseNpgsql(_fixture.Postgres.GetConnectionString())
            .Options;

        await using var db = new AppDbContext(options);
        await db.Database.MigrateAsync();
        await db.Database.ExecuteSqlRawAsync(
            "TRUNCATE TABLE dose_events, dose_schedules, medications, devices CASCADE");
    }

    private static async Task<(Guid DeviceId, string ApiKey)> RegisterDeviceAsync(HttpClient client)
    {
        var response = await client.PostAsJsonAsync("/api/devices/register", new { name = "ESP32-01" });
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        using var doc = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        return (doc.RootElement.GetProperty("deviceId").GetGuid(),
            doc.RootElement.GetProperty("apiKey").GetString()!);
    }

    [Fact]
    public async Task Register_device_returns_device_id_and_32char_hex_api_key()
    {
        using var client = _factory!.CreateClient();

        var (deviceId, apiKey) = await RegisterDeviceAsync(client);

        Assert.NotEqual(Guid.Empty, deviceId);
        Assert.Matches("^[0-9a-f]{32}$", apiKey);
    }

    [Fact]
    public async Task Medication_crud_and_schedules()
    {
        using var client = _factory!.CreateClient();
        var (deviceId, _) = await RegisterDeviceAsync(client);

        // Validation: a medication requires at least one schedule.
        var bad = await client.PostAsJsonAsync("/api/medications", new
        {
            deviceId,
            name = "Losartan",
            dosage = "50mg",
            isActive = true,
            schedules = Array.Empty<object>()
        });
        Assert.Equal(HttpStatusCode.BadRequest, bad.StatusCode);

        // Create with two schedules.
        var create = await client.PostAsJsonAsync("/api/medications", new
        {
            deviceId,
            name = "Losartan",
            dosage = "50mg",
            isActive = true,
            schedules = new[]
            {
                new { localTime = "08:00", daysOfWeek = 127, isActive = true },
                new { localTime = "20:00", daysOfWeek = 127, isActive = true }
            }
        });
        Assert.Equal(HttpStatusCode.Created, create.StatusCode);

        using var medDoc = JsonDocument.Parse(await create.Content.ReadAsStringAsync());
        var medicationId = medDoc.RootElement.GetProperty("id").GetGuid();
        Assert.Equal(2, medDoc.RootElement.GetProperty("schedules").GetArrayLength());

        // List by device.
        var list = await client.GetFromJsonAsync<JsonElement>($"/api/medications?deviceId={deviceId}");
        Assert.Contains(list.EnumerateArray(), m => m.GetProperty("id").GetGuid() == medicationId);

        // Update name.
        var update = await client.PutAsJsonAsync($"/api/medications/{medicationId}", new { name = "Losartan 100mg" });
        Assert.Equal(HttpStatusCode.NoContent, update.StatusCode);

        // Add a third schedule.
        var addSchedule = await client.PostAsJsonAsync($"/api/medications/{medicationId}/schedules",
            new { localTime = "12:00", daysOfWeek = 127, isActive = true });
        Assert.Equal(HttpStatusCode.Created, addSchedule.StatusCode);

        using var schedDoc = JsonDocument.Parse(await addSchedule.Content.ReadAsStringAsync());
        var scheduleId = schedDoc.RootElement.GetProperty("id").GetGuid();

        var schedules = await client.GetFromJsonAsync<JsonElement>($"/api/medications/{medicationId}/schedules");
        Assert.Equal(3, schedules.GetArrayLength());

        // Soft-delete the schedule.
        var deleteSchedule = await client.DeleteAsync($"/api/medications/{medicationId}/schedules/{scheduleId}");
        Assert.Equal(HttpStatusCode.NoContent, deleteSchedule.StatusCode);

        var schedulesAfter = await client.GetFromJsonAsync<JsonElement>($"/api/medications/{medicationId}/schedules");
        Assert.Equal(2, schedulesAfter.GetArrayLength());

        // Soft-delete the medication.
        var deleteMedication = await client.DeleteAsync($"/api/medications/{medicationId}");
        Assert.Equal(HttpStatusCode.NoContent, deleteMedication.StatusCode);

        var listAfter = await client.GetFromJsonAsync<JsonElement>($"/api/medications?deviceId={deviceId}");
        Assert.DoesNotContain(listAfter.EnumerateArray(), m => m.GetProperty("id").GetGuid() == medicationId);
    }

    [Fact]
    public async Task Taken_event_is_reflected_in_events_and_adherence()
    {
        using var client = _factory!.CreateClient();
        var (deviceId, apiKey) = await RegisterDeviceAsync(client);

        var create = await client.PostAsJsonAsync("/api/medications", new
        {
            deviceId,
            name = "Losartan",
            dosage = "50mg",
            isActive = true,
            schedules = new[] { new { localTime = "08:00", daysOfWeek = 127, isActive = true } }
        });
        Assert.Equal(HttpStatusCode.Created, create.StatusCode);

        using var medDoc = JsonDocument.Parse(await create.Content.ReadAsStringAsync());
        var medicationId = medDoc.RootElement.GetProperty("id").GetGuid();

        client.DefaultRequestHeaders.Add("X-Api-Key", apiKey);

        var taken = await client.PostAsJsonAsync($"/api/devices/{deviceId}/events", new
        {
            type = "taken",
            medicationId,
            occurredAtUtc = "2026-09-10T12:00:00Z"
        });
        Assert.Equal(HttpStatusCode.OK, taken.StatusCode);

        var events = await client.GetFromJsonAsync<JsonElement>($"/api/events?medicationId={medicationId}");
        var takenEvent = Assert.Single(events.EnumerateArray());
        Assert.Equal("taken", takenEvent.GetProperty("type").GetString());
        Assert.Equal(new DateTime(2026, 9, 10, 11, 0, 0, DateTimeKind.Utc),
            takenEvent.GetProperty("scheduledUtc").GetDateTime());

        var adherence = await client.GetFromJsonAsync<JsonElement>("/api/adherence?from=2026-09-10&to=2026-09-10");
        var day = Assert.Single(adherence.EnumerateArray());
        Assert.Equal(1, day.GetProperty("taken").GetInt32());
        Assert.Equal(0, day.GetProperty("missed").GetInt32());
        Assert.Equal(0, day.GetProperty("pending").GetInt32());
    }

    [Fact]
    public async Task Device_state_and_events_require_valid_api_key()
    {
        using var client = _factory!.CreateClient();
        var (deviceId, apiKey) = await RegisterDeviceAsync(client);

        // No header.
        var noHeader = await client.GetAsync($"/api/devices/{deviceId}/state");
        Assert.Equal(HttpStatusCode.Unauthorized, noHeader.StatusCode);

        // Wrong key.
        client.DefaultRequestHeaders.Add("X-Api-Key", "00000000000000000000000000000000");
        var wrongKey = await client.GetAsync($"/api/devices/{deviceId}/state");
        Assert.Equal(HttpStatusCode.Unauthorized, wrongKey.StatusCode);

        // Valid key.
        client.DefaultRequestHeaders.Remove("X-Api-Key");
        client.DefaultRequestHeaders.Add("X-Api-Key", apiKey);
        var ok = await client.GetAsync($"/api/devices/{deviceId}/state");
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);

        // Events endpoint requires the key too.
        using var unauthenticatedClient = _factory!.CreateClient();
        var eventResponse = await unauthenticatedClient.PostAsJsonAsync($"/api/devices/{deviceId}/events",
            new { type = "taken", medicationId = Guid.Empty });
        Assert.Equal(HttpStatusCode.Unauthorized, eventResponse.StatusCode);
    }
}