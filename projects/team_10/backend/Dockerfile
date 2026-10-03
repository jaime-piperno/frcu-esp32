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
EXPOSE 8080
ENV ASPNETCORE_URLS=http://+:8080
COPY --from=build /app/publish .
ENTRYPOINT ["dotnet", "RecuerdaMed.Api.dll"]