using System.Text.Json.Serialization;

namespace RecuerdaMed.Api.Domain;

[JsonConverter(typeof(JsonStringEnumConverter<DoseUnit>))]
public enum DoseUnit : short
{
    Milligrams = 0,
    Tablets = 1
}
