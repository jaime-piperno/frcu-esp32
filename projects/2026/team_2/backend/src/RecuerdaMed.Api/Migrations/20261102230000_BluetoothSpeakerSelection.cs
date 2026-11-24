using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using RecuerdaMed.Api.Persistence;

#nullable disable

namespace RecuerdaMed.Api.Migrations;

[DbContext(typeof(AppDbContext))]
[Migration("20261102230000_BluetoothSpeakerSelection")]
public sealed class BluetoothSpeakerSelection : Migration
{
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.AddColumn<string>(
            name: "BluetoothSpeakerAddress",
            table: "devices",
            type: "character varying(17)",
            maxLength: 17,
            nullable: true);

        migrationBuilder.AddColumn<string>(
            name: "BluetoothSpeakerName",
            table: "devices",
            type: "character varying(100)",
            maxLength: 100,
            nullable: true);
    }

    protected override void Down(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.DropColumn(name: "BluetoothSpeakerAddress", table: "devices");
        migrationBuilder.DropColumn(name: "BluetoothSpeakerName", table: "devices");
    }
}
