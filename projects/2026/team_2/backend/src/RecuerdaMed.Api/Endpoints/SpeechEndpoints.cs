using RecuerdaMed.Api.Services;

namespace RecuerdaMed.Api.Endpoints;

public static class SpeechEndpoints
{
    public static WebApplication MapSpeechEndpoints(this WebApplication app)
    {
        app.MapGet("/api/devices/{deviceId:guid}/speech", GetSpeechAsync)
            .WithTags("Speech");
        return app;
    }

    private static async Task<IResult> GetSpeechAsync(
        Guid deviceId, AlertStateService alerts,
        SpeechSynthesisService speech, CancellationToken ct)
    {
        var state = await alerts.UpdateAndGetStateAsync(deviceId, ct);
        if (state.Alerts.Count == 0) return Results.NotFound();
        var wav = await speech.SynthesizeAsync(state.Alerts, ct);
        return Results.File(wav, "audio/wav");
    }
}
