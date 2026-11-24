# Build stage
FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src

COPY ["src/RecuerdaMed.Api/RecuerdaMed.Api.csproj", "src/RecuerdaMed.Api/"]
RUN dotnet restore "src/RecuerdaMed.Api/RecuerdaMed.Api.csproj"

COPY src/RecuerdaMed.Api/ src/RecuerdaMed.Api/
WORKDIR /src/src/RecuerdaMed.Api
RUN dotnet publish "RecuerdaMed.Api.csproj" -c Release -o /app/publish --no-restore

# Runtime stage
FROM mcr.microsoft.com/dotnet/aspnet:10.0 AS final
WORKDIR /app
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        ca-certificates curl python3 python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /opt/piper \
    && /opt/piper/bin/pip install --no-cache-dir piper-tts==1.8.0 \
    && mkdir -p /opt/piper/voices \
    && curl -fsSL --retry 3 \
        -o /opt/piper/voices/es_AR-daniela-high.onnx \
        https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_AR/daniela/high/es_AR-daniela-high.onnx \
    && echo '7ceb1fc0dab349418c5b54a639ae9ee595212d7c9ea422220d8419163d5cc985  /opt/piper/voices/es_AR-daniela-high.onnx' | sha256sum -c - \
    && curl -fsSL --retry 3 \
        -o /opt/piper/voices/es_AR-daniela-high.onnx.json \
        https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_AR/daniela/high/es_AR-daniela-high.onnx.json
EXPOSE 8080
ENV ASPNETCORE_URLS=http://+:8080
COPY --from=build /app/publish .
ENTRYPOINT ["dotnet", "RecuerdaMed.Api.dll"]
