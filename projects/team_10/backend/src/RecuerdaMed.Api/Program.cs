using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using NodaTime;
using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Endpoints;
using RecuerdaMed.Api.Hubs;
using RecuerdaMed.Api.Persistence;
using RecuerdaMed.Api.Services;

var builder = WebApplication.CreateBuilder(args);

var connectionString = builder.Configuration.GetConnectionString("Default")
    ?? throw new InvalidOperationException("Connection string 'Default' is missing.");

builder.Services.AddOpenApi();

builder.Services.AddDbContext<AppDbContext>(options => options.UseNpgsql(connectionString));

builder.Services.Configure<AlertingOptions>(builder.Configuration.GetSection(AlertingOptions.SectionName));
builder.Services.Configure<MqttOptions>(builder.Configuration.GetSection(MqttOptions.SectionName));

builder.Services.AddSingleton<IClock>(SystemClock.Instance);
builder.Services.AddScoped<AlertStateService>();
builder.Services.AddScoped<DeviceAuthService>();
builder.Services.AddSingleton<AdherenceNotifier>();
builder.Services.AddSingleton<StateChangeSignal>();
builder.Services.AddSingleton<MqttService>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<MqttService>());
builder.Services.AddSingleton<StatePublisherService>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<StatePublisherService>());

builder.Services.AddSignalR();

// Dashboard (React/Vite) runs on its own origin during development.
builder.Services.AddCors(options => options.AddPolicy("Dashboard", policy => policy
    .WithOrigins("http://localhost:5173")
    .AllowAnyHeader()
    .AllowAnyMethod()
    .AllowCredentials()));

var app = builder.Build();

app.Use(async (context, next) =>
{
    try
    {
        await next();
    }
    catch (NotFoundException nf)
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        await context.Response.WriteAsJsonAsync(new ProblemDetails
        {
            Status = StatusCodes.Status404NotFound,
            Title = "Not Found",
            Detail = nf.Message
        });
    }
    catch (Exception ex)
    {
        var logger = context.RequestServices.GetRequiredService<ILogger<Program>>();
        logger.LogError(ex, "Unhandled exception while processing {Method} {Path}",
            context.Request.Method, context.Request.Path);

        context.Response.StatusCode = StatusCodes.Status500InternalServerError;
        await context.Response.WriteAsJsonAsync(new ProblemDetails
        {
            Status = StatusCodes.Status500InternalServerError,
            Title = "Internal Server Error",
            Detail = "An unexpected error occurred."
        });
    }
});

app.MapOpenApi();

app.UseCors("Dashboard");

app.MapHub<AdherenceHub>("/hubs/adherence");

app.MapDeviceEndpoints();
app.MapMedicationEndpoints();
app.MapEventEndpoints();
app.MapAdherenceEndpoints();
app.MapDashboardEndpoints();

if (app.Environment.IsDevelopment() || app.Environment.IsEnvironment("Testing"))
{
    using var scope = app.Services.CreateScope();
    var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();
    db.Database.Migrate();
}

if (app.Environment.IsDevelopment())
{
    await SeedDevelopmentDataAsync(app.Services);
}

app.Run();

static async Task SeedDevelopmentDataAsync(IServiceProvider services)
{
    using var scope = services.CreateScope();
    var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();

    if (await db.Devices.AnyAsync())
        return;

    var apiKey = DeviceAuthService.GenerateApiKey();
    var device = new Device
    {
        Id = Guid.NewGuid(),
        Name = "ESP32-01",
        ApiKey = apiKey,
        CreatedAtUtc = DateTime.UtcNow
    };

    var allDays = DayOfWeekFlags.Mon | DayOfWeekFlags.Tue | DayOfWeekFlags.Wed
                  | DayOfWeekFlags.Thu | DayOfWeekFlags.Fri | DayOfWeekFlags.Sat
                  | DayOfWeekFlags.Sun;

    var medication = new Medication
    {
        Id = Guid.NewGuid(),
        DeviceId = device.Id,
        Name = "Losartan 50mg",
        Dosage = "50mg",
        Schedules =
        [
            new DoseSchedule { Id = Guid.NewGuid(), LocalTime = new TimeOnly(8, 0), DaysOfWeek = allDays },
            new DoseSchedule { Id = Guid.NewGuid(), LocalTime = new TimeOnly(20, 0), DaysOfWeek = allDays }
        ]
    };

    db.Devices.Add(device);
    db.Medications.Add(medication);
    await db.SaveChangesAsync();

    var logger = scope.ServiceProvider.GetRequiredService<ILogger<Program>>();
    logger.LogWarning("DEVICE API KEY: {ApiKey}", apiKey);
}

public partial class Program;