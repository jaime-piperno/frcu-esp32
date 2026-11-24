namespace RecuerdaMed.Api.Domain;

[Flags]
public enum DayOfWeekFlags : byte
{
    None = 0,
    Mon = 1,
    Tue = 2,
    Wed = 4,
    Thu = 8,
    Fri = 16,
    Sat = 32,
    Sun = 64
}