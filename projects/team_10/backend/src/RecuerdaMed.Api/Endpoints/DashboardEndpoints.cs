using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

/// <summary>
/// Read-only endpoints for the web dashboard (no device API key required).
/// The device-facing endpoints (GET /state, POST /events) keep their X-Api-Key
/// protection; the dashboard observes through this group + SignalR.
/// </summary>
public static class DashboardEndpoints
{
    public static WebApplication MapDashboardEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/dashboard").WithTags("Dashboard");

        group.MapGet("/state/{deviceId:guid}", GetStateAsync);

        return app;
    }

    private static async Task<IResult> GetStateAsync(
        Guid deviceId, AlertStateService alerts, CancellationToken ct)
    {
        try
        {
            var state = await alerts.UpdateAndGetStateAsync(deviceId, ct);
            return Results.Ok(state);
        }
        catch (NotFoundException)
        {
            return Results.NotFound();
        }
    }
}