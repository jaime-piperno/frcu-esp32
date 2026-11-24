namespace RecuerdaMed.Api.Services;

/// <summary>Wake signal for the state publisher: any code that mutates device state
/// (MQTT/REST device events, medication CRUD) calls Notify() so the publisher can
/// recompute its next wake-up instead of sleeping until a stale schedule.</summary>
public sealed class StateChangeSignal
{
    private readonly SemaphoreSlim _signal = new(0, 1);

    public void Notify()
    {
        try { _signal.Release(); }
        catch (SemaphoreFullException) { /* already signaled */ }
    }

    public Task WaitAsync(CancellationToken ct) => _signal.WaitAsync(ct);
}