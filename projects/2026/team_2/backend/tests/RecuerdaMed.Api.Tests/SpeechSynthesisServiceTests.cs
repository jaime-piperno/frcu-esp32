using RecuerdaMed.Api.Domain;
using RecuerdaMed.Api.Services;
using Xunit;

namespace RecuerdaMed.Api.Tests;

public sealed class SpeechSynthesisServiceTests
{
    [Fact]
    public void Composes_milligrams_and_optional_message_in_spanish()
    {
        var alert = new AlertInfo(Guid.NewGuid(), Guid.NewGuid(), "Enalapril", 10,
            DoseUnit.Milligrams, "Mario, acordate que son las pastillas amarillas");

        Assert.Equal("Es hora de tomar Enalapril. La dosis es 10 miligramos. Mario, acordate que son las pastillas amarillas",
            SpeechSynthesisService.ComposeText(alert));
    }

    [Fact]
    public void Uses_singular_for_one_tablet_without_custom_message()
    {
        var alert = new AlertInfo(Guid.NewGuid(), Guid.NewGuid(), "Losartan", 1,
            DoseUnit.Tablets, null);

        Assert.Equal("Es hora de tomar Losartan. La dosis es una pastilla.",
            SpeechSynthesisService.ComposeText(alert));

        Assert.Contains("La dosis es 2 pastillas.",
            SpeechSynthesisService.ComposeText(alert with { DosageAmount = 2 }));
    }
}
