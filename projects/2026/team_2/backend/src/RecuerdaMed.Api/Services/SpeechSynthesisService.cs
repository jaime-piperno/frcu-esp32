using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using RecuerdaMed.Api.Domain;

namespace RecuerdaMed.Api.Services;

/// <summary>Produces 11.025 kHz mono 16-bit PCM WAV for streaming to the ESP32.</summary>
public sealed class SpeechSynthesisService
{
    private const int OutputRate = 11025;
    private const string PiperPython = "/opt/piper/bin/python";
    private const string PiperModel = "/opt/piper/voices/es_AR-daniela-high.onnx";

    public static string ComposeText(AlertInfo alert)
    {
        var amount = alert.DosageAmount.ToString("0.###", CultureInfo.GetCultureInfo("es-AR"));
        var quantity = alert.DosageUnit == DoseUnit.Milligrams
            ? $"{amount} miligramos"
            : alert.DosageAmount == 1 ? "una pastilla" : $"{amount} pastillas";
        var message = string.IsNullOrWhiteSpace(alert.Message)
            ? string.Empty
            : $" {Regex.Replace(alert.Message.Trim(), @"\s+", " ")}";
        var name = Regex.Replace(alert.Name.Trim(), @"\s+", " ");
        return $"Es hora de tomar {name}. La dosis es {quantity}.{message}";
    }

    public async Task<byte[]> SynthesizeAsync(IReadOnlyList<AlertInfo> alerts, CancellationToken ct)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(TimeSpan.FromSeconds(25));

        var outputPath = Path.Combine(Path.GetTempPath(), $"recuerdamed-speech-{Guid.NewGuid():N}.wav");

        using var process = new Process
        {
            StartInfo = new ProcessStartInfo(PiperPython)
            {
                WorkingDirectory = "/opt/piper/voices",
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false
            }
        };
        foreach (var argument in new[] { "-m", "piper", "-m", PiperModel, "-f", outputPath, "--", string.Join(" ", alerts.Select(ComposeText)) })
            process.StartInfo.ArgumentList.Add(argument);

        var started = false;
        try
        {
            started = process.Start();
            if (!started) throw new InvalidOperationException("Piper could not start.");
            var readOutput = process.StandardOutput.ReadToEndAsync(timeout.Token);
            var readError = process.StandardError.ReadToEndAsync(timeout.Token);
            await Task.WhenAll(readOutput, readError, process.WaitForExitAsync(timeout.Token));
            if (process.ExitCode != 0)
                throw new InvalidOperationException($"Piper failed: {await readError}");
            return ConvertToA2dpWav(await File.ReadAllBytesAsync(outputPath, timeout.Token));
        }
        catch
        {
            if (started && !process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync(CancellationToken.None);
            }
            throw;
        }
        finally
        {
            if (File.Exists(outputPath)) File.Delete(outputPath);
        }
    }

    private static byte[] ConvertToA2dpWav(byte[] wav)
    {
        if (wav.Length < 44 || Encoding.ASCII.GetString(wav, 0, 4) != "RIFF" ||
            Encoding.ASCII.GetString(wav, 8, 4) != "WAVE")
            throw new InvalidDataException("Speech output is not a WAV file.");

        int sampleRate = 0, channels = 0, bits = 0, dataOffset = -1;
        for (var offset = 12; offset + 8 <= wav.Length;)
        {
            var kind = Encoding.ASCII.GetString(wav, offset, 4);
            var size = BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(offset + 4, 4));
            if (kind == "fmt " && size >= 16 && offset + 24 <= wav.Length)
            {
                if (BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(offset + 8, 2)) != 1)
                    throw new InvalidDataException("Speech output must be PCM.");
                channels = BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(offset + 10, 2));
                sampleRate = (int)BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(offset + 12, 4));
                bits = BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(offset + 22, 2));
            }
            if (kind == "data") { dataOffset = offset + 8; break; }
            if (size > wav.Length - offset - 8) break;
            offset += 8 + (int)size + (int)(size & 1);
        }
        if (channels != 1 || bits != 16 || sampleRate < 8000 || dataOffset < 0)
            throw new InvalidDataException("Speech output must be mono 16-bit PCM.");

        var inputFrames = (wav.Length - dataOffset) / 2;
        var voiceFrames = (int)((long)inputFrames * OutputRate / sampleRate);
        // Give the A2DP sink time to start and drain its decoder before STOP.
        var leadingSilence = OutputRate / 5;
        var outputFrames = leadingSilence + voiceFrames + OutputRate / 2;
        var result = new byte[44 + outputFrames * 2];
        Encoding.ASCII.GetBytes("RIFF").CopyTo(result, 0);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(4), (uint)(result.Length - 8));
        Encoding.ASCII.GetBytes("WAVEfmt ").CopyTo(result, 8);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(16), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(20), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(22), 1);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(24), OutputRate);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(28), OutputRate * 2);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(32), 2);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(34), 16);
        Encoding.ASCII.GetBytes("data").CopyTo(result, 36);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(40), (uint)(outputFrames * 2));

        for (var i = 0; i < voiceFrames; i++)
        {
            var position = (double)i * sampleRate / OutputRate;
            var first = (int)position;
            var second = Math.Min(first + 1, inputFrames - 1);
            var left = BinaryPrimitives.ReadInt16LittleEndian(wav.AsSpan(dataOffset + first * 2, 2));
            var right = BinaryPrimitives.ReadInt16LittleEndian(wav.AsSpan(dataOffset + second * 2, 2));
            var sample = (short)Math.Round(left + (right - left) * (position - first));
            var outputOffset = 44 + (leadingSilence + i) * 2;
            BinaryPrimitives.WriteInt16LittleEndian(result.AsSpan(outputOffset, 2), sample);
        }
        return result;
    }
}
